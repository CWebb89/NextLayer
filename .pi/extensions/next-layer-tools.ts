/**
 * next-layer tools: the four commands this project's verification loop needs,
 * with the rules that make them honest baked in rather than remembered.
 *
 *   nl_build   cmake build inside the memory-capped cgroup, bounded diagnostics
 *   nl_oracle  G-code oracle capture/verify, parsed into a verdict
 *   nl_smoke   one headless frame, coverage read from the frame itself
 *   nl_cockpit drive and capture a real window through tools/cockpit
 *
 * Every tool returns a verdict the model cannot invent: an exit code, a match
 * count, a measured coverage figure, or a captured PNG it has to look at.
 */

import { spawn } from "node:child_process";
import { createWriteStream } from "node:fs";
import { readFile, stat } from "node:fs/promises";
import type { ExtensionAPI, ExtensionContext } from "@earendil-works/pi-coding-agent";
import { StringEnum } from "@earendil-works/pi-ai";
import { Type } from "typebox";

const REPO = "/home/cwebb/dev/next-layer";
const CAPTURE_LIMIT = 2 * 1024 * 1024;
const BUILD_MEMORY = "MemoryMax=8G";
const BUILD_SWAP = "MemorySwapMax=2G";
const QT_BIN = `${REPO}/upstream/build/src/slic3r-app-qt/next-layer-qt`;
const DEFAULT_MODEL = `${REPO}/oracle/models/cube_20.stl`;

const RUNNER_BIN = {
	qt: QT_BIN,
	app: `${REPO}/upstream/build/src/slic3r-app-spike/next-layer-spike`,
	launcher: `${REPO}/upstream/build/src/slic3r-app-launcher/slic3r-app-launcher`,
};

type ExecOptions = {
	cwd?: string;
	env?: Record<string, string>;
	timeoutMs?: number;
	signal?: AbortSignal;
	logPath?: string;
};

type ExecResult = { code: number; stdout: string; stderr: string };

async function exec(command: string, args: string[], options: ExecOptions = {}): Promise<ExecResult> {
	const { cwd = REPO, env = {}, timeoutMs = 1_800_000, signal, logPath } = options;
	return new Promise<ExecResult>((resolve, reject) => {
		const child = spawn(command, args, { cwd, env: { ...process.env, ...env }, detached: true });
		const log = logPath ? createWriteStream(logPath) : null;
		let stdout = "";
		let stderr = "";
		const keep = (current: string, chunk: Buffer) =>
			current.length >= CAPTURE_LIMIT ? current : current + chunk.toString();
		child.stdout?.on("data", (chunk: Buffer) => {
			stdout = keep(stdout, chunk);
			log?.write(chunk);
		});
		child.stderr?.on("data", (chunk: Buffer) => {
			stderr = keep(stderr, chunk);
			log?.write(chunk);
		});

		let finished = false;
		const stop = () => {
			if (finished || !child.pid) return;
			try {
				process.kill(-child.pid, "SIGTERM");
			} catch {
				child.kill("SIGTERM");
			}
		};
		const timer = setTimeout(stop, timeoutMs);
		const onAbort = () => stop();
		signal?.addEventListener("abort", onAbort, { once: true });

		child.on("error", (error) => {
			finished = true;
			clearTimeout(timer);
			signal?.removeEventListener("abort", onAbort);
			log?.end();
			reject(error);
		});
		child.on("close", async (code) => {
			finished = true;
			clearTimeout(timer);
			signal?.removeEventListener("abort", onAbort);
			if (log) await new Promise<void>((done) => log.end(done));
			resolve({ code: code ?? -1, stdout, stderr });
		});
	});
}

function tail(text: string, lines: number): string {
	return text.trimEnd().split("\n").slice(-lines).join("\n");
}

function clip(line: string, width = 300): string {
	return line.length > width ? `${line.slice(0, width)}…` : line;
}

function extractDiagnostics(text: string) {
	const errors: string[] = [];
	const warnings: string[] = [];
	const diagnostic = /^[^\s:][^:]*:\d+:(?:\d+:)?\s*(fatal error|error|warning|note):/;
	const failure = /^(FAILED:|ninja: build stopped|make(\[\d+\])?: \*\*\*)/;
	for (const raw of text.split("\n")) {
		const line = raw.trim();
		if (!line) continue;
		if (failure.test(line)) {
			errors.push(clip(line));
			continue;
		}
		const match = diagnostic.exec(line);
		if (!match) continue;
		if (match[1] === "warning") warnings.push(clip(line));
		else if (match[1] !== "note") errors.push(clip(line));
	}
	return { errors: [...new Set(errors)], warnings: [...new Set(warnings)] };
}

async function imageBlock(ctx: ExtensionContext, path: string) {
	if (!ctx.model?.input?.includes("image")) return null;
	try {
		const info = await stat(path);
		if (!info.isFile() || info.size === 0) return null;
		const data = await readFile(path);
		return { type: "image" as const, data: data.toString("base64"), mimeType: "image/png" };
	} catch {
		return null;
	}
}

async function exists(path: string): Promise<boolean> {
	try {
		return (await stat(path)).isFile();
	} catch {
		return false;
	}
}

const buildSchema = Type.Object({
	target: Type.String({ description: "CMake target, e.g. next-layer-qt (default) or next-layer-spike." }),
	jobs: Type.Optional(Type.Number({ description: "Parallel compile jobs, 1-16. Default 4." })),
});

const oracleSchema = Type.Object({
	action: StringEnum(["verify", "capture"] as const),
	runner: Type.Optional(StringEnum(["qt", "app", "launcher"] as const)),
	bin: Type.Optional(Type.String({ description: "Slicer binary; defaults to the build tree path for the runner." })),
});

const smokeSchema = Type.Object({
	mode: StringEnum(["model", "preview", "slider"] as const),
	model: Type.Optional(Type.String({ description: "STL to open. Default oracle/models/cube_20.stl." })),
	gcode: Type.Optional(Type.String({ description: "G-code for preview/slider modes." })),
	out: Type.Optional(Type.String({ description: "PNG path. Default /tmp/nl-smoke.png." })),
	view: Type.Optional(Type.String({ description: "Camera YAW,PITCH,ZOOM, e.g. 90,-6,0.55." })),
	layer: Type.Optional(Type.Number({ description: "Open the preview at 1-based layer N." })),
});

const cockpitSchema = Type.Object({
	command: StringEnum([
		"start",
		"stop",
		"status",
		"windows",
		"focus",
		"shot",
		"ocr",
		"find-text",
		"click-text",
		"click",
		"move",
		"drag",
		"scroll",
		"key",
		"type",
		"run",
	] as const),
	args: Type.Optional(Type.Array(Type.String(), { description: "Arguments for the cockpit subcommand." })),
	app: Type.Optional(Type.String({ description: "Cockpit session name, if more than one is running." })),
});

export default function (pi: ExtensionAPI) {
	pi.registerTool({
		name: "nl_build",
		label: "next-layer build",
		description:
			"Build a next-layer CMake target inside the mandatory memory-capped systemd scope and report only the diagnostics. Never run cmake through bash.",
		promptSnippet: "Build a next-layer target in the memory-capped cgroup (nl_build)",
		promptGuidelines: [
			"Use nl_build, never bash, for cmake builds: it applies the cgroup memory cap this machine needs and returns bounded diagnostics.",
		],
		parameters: buildSchema,
		executionMode: "sequential",
		async execute(_id, params, signal) {
			const target = params.target.trim();
			if (!/^[A-Za-z0-9_.:@-]+$/.test(target)) throw new Error(`invalid target: ${params.target}`);
			const jobs = Math.min(16, Math.max(1, Math.round(params.jobs ?? 4)));
			const started = Date.now();
			const logPath = `/tmp/nl-build-${target}-${started}.log`;
			const script = `export CMAKE_BUILD_PARALLEL_LEVEL=${jobs}; cmake --build upstream/build --target ${target} -j${jobs}`;
			const result = await exec(
				"systemd-run",
				[
					"--user",
					"--scope",
					"--collect",
					"-p",
					BUILD_MEMORY,
					"-p",
					BUILD_SWAP,
					`--working-directory=${REPO}`,
					"/bin/sh",
					"-c",
					script,
				],
				{ signal, logPath, timeoutMs: 2_400_000 },
			);
			const seconds = ((Date.now() - started) / 1000).toFixed(1);
			const { errors, warnings } = extractDiagnostics(`${result.stdout}\n${result.stderr}`);
			const warningNote =
				warnings.length === 0
					? "0 warnings"
					: `${warnings.length} warning(s):\n${warnings.slice(0, 5).map((l) => `  ${l}`).join("\n")}`;
			const verdict =
				result.code === 0
					? `${target}: ok in ${seconds}s (jobs ${jobs}, ${BUILD_MEMORY}, ${BUILD_SWAP})`
					: `${target}: FAILED, exit ${result.code} after ${seconds}s`;
			const body = [
				verdict,
				`log: ${logPath}`,
				errors.length === 0 ? "0 errors" : `${errors.length} error(s):\n${errors.slice(0, 25).map((l) => `  ${l}`).join("\n")}`,
				warningNote,
				errors.length === 0 && result.code !== 0 ? `stderr tail:\n${tail(result.stderr, 12)}` : "",
			]
				.filter(Boolean)
				.join("\n");
			return {
				content: [{ type: "text", text: body }],
				details: { summary: verdict },
				isError: result.code !== 0,
			};
		},
	});

	pi.registerTool({
		name: "nl_oracle",
		label: "next-layer oracle",
		description:
			"Run the G-code regression oracle. verify re-slices all 11 cases and compares fingerprints; capture rewrites the baselines and must only follow a deliberate engine or model change.",
		promptSnippet: "Prove slicing parity with the G-code oracle (nl_oracle)",
		promptGuidelines: [
			"Use nl_oracle verify after any change touching slicing, settings, presets or engine code, and report its verdict verbatim rather than a summary.",
			"Use nl_oracle capture only when the change was meant to alter the toolpaths; it overwrites oracle/baseline.",
		],
		parameters: oracleSchema,
		executionMode: "sequential",
		async execute(_id, params, signal) {
			const runner = params.runner ?? "qt";
			const binary = params.bin ?? RUNNER_BIN[runner];
			const args = [`${REPO}/oracle/oracle.py`, params.action, "--runner", runner, "--bin", binary];
			const result = await exec("python3", args, { signal, timeoutMs: 1_800_000 });
			const cases: { name: string; status: string; detail: string }[] = [];
			const diffs: string[] = [];
			let summary = "";
			for (const raw of `${result.stdout}\n${result.stderr}`.split("\n")) {
				const line = raw.trimEnd();
				const match = /^\s{2}(ok|CHANGED|FAIL|MISSING|SKIPPED)\s+(\S+)\s*(.*)$/.exec(line);
				if (match) {
					cases.push({ name: match[2], status: match[1], detail: match[3].trim() });
					continue;
				}
				const diff = /^\s+diff -u (\S+) (\S+)$/.exec(line);
				if (diff) diffs.push(`  ${diff[2]}`);
				const total = /(\d+)\/(\d+) match baseline '([^']*)'/.exec(line);
				if (total) summary = `${total[1]}/${total[2]} match baseline '${total[3]}'`;
			}
			const bad = cases.filter((c) => c.status !== "ok" && c.status !== "SKIPPED");
			const skipped = cases.filter((c) => c.status === "SKIPPED").map((c) => c.name);
			const header = summary || `oracle ${params.action} produced no verdict (exit ${result.code})`;
			const body = [
				`oracle ${params.action} (runner ${runner}, binary ${binary}): ${header}`,
				bad.length === 0
					? ""
					: bad
							.map(
								(c) =>
									`  ${c.status.padEnd(8)} ${c.name}${c.detail ? `  ${c.detail}` : ""}${
										c.status === "CHANGED" ? "  (actual written beside the baseline)" : ""
									}`,
							)
							.join("\n"),
				skipped.length > 0 ? `  skipped: ${skipped.join(", ")}` : "",
				diffs.length > 0 ? `diff against baseline:\n${diffs.join("\n")}` : "",
				result.code !== 0 && cases.length === 0 ? `output tail:\n${tail(`${result.stdout}${result.stderr}`, 15)}` : "",
			]
				.filter(Boolean)
				.join("\n");
			return {
				content: [{ type: "text", text: body }],
				details: { summary: header },
				isError: result.code !== 0,
			};
		},
	});

	pi.registerTool({
		name: "nl_smoke",
		label: "next-layer headless frame",
		description:
			"Render one frame with no window on the desktop and report measured coverage, plus the PNG for the model to look at. model mode uses eglfs; preview and slider modes use wayland because eglfs cannot host a GL preview surface.",
		promptSnippet: "Render and inspect a headless next-layer frame (nl_smoke)",
		promptGuidelines: [
			"Use nl_smoke before claiming any viewport, preview or QML change works; a passing exit code without a measured coverage figure is not evidence.",
		],
		parameters: smokeSchema,
		executionMode: "sequential",
		async execute(_id, params, signal, _onUpdate, ctx) {
			const model = params.model ?? DEFAULT_MODEL;
			const out = params.out ?? "/tmp/nl-smoke.png";
			if (!(await exists(model))) throw new Error(`no such model: ${model}`);
			const args = [model];
			if (params.mode !== "model") {
				if (!params.gcode) throw new Error(`${params.mode} mode needs gcode`);
				if (!(await exists(params.gcode))) throw new Error(`no such gcode: ${params.gcode}`);
				args.push("--preview", params.gcode);
			}
			if (params.mode === "slider") args.push("--slider-test");
			if (params.layer !== undefined) args.push("--layer", String(Math.max(1, Math.round(params.layer))));
			if (params.view) args.push("--view", params.view);
			args.push("--smoke", "--shot", out);
			const env = {
				QT_QPA_PLATFORM: params.mode === "model" ? "eglfs" : "wayland",
				QT_ASSUME_STDERR_HAS_CONSOLE: "1",
			};
			const result = await exec(QT_BIN, args, { signal, env, timeoutMs: 240_000 });
			const combined = `${result.stdout}\n${result.stderr}`;
			const frame = /frame (\d+)x(\d+), covered ([\d.]+)%/.exec(combined);
			const frames = /gcode view: (\d+) frames/.exec(combined);
			const stepped = /layer by layer/i.test(combined);
			const problems = combined
				.split("\n")
				.filter((line) => /^(smoke|gcode-shot|preview|qt\.qpa\.wayland\.textinput):/.test(line.trim()) && !/textinput/.test(line))
				.map((line) => line.trim());
			const verdict =
				result.code === 0 && frame
					? `smoke ${params.mode}: ok, ${frame[3]}% covered, ${frame[1]}x${frame[2]}, ${out}`
					: `smoke ${params.mode}: FAILED, exit ${result.code}`;
			const body = [
				verdict,
				frame ? `coverage ${frame[3]}%` : "no coverage line: the frame was never measured",
				frames ? `gcode view rendered ${frames[1]} frame(s)` : "",
				params.mode === "slider" ? `slider test: ${stepped ? "ended layer by layer" : "did NOT report layer by layer"}` : "",
				problems.length > 0 ? problems.join("\n") : "",
				result.code !== 0 ? tail(combined, 15) : "",
				ctx.model?.input?.includes("image") ? "" : `frame saved at ${out}`,
			]
				.filter(Boolean)
				.join("\n");
			const image = await imageBlock(ctx, out);
			return {
				content: image ? [{ type: "text", text: body }, image] : [{ type: "text", text: body }],
				details: { summary: verdict },
				isError: result.code !== 0 || (params.mode === "slider" && !stepped),
			};
		},
	});

	pi.registerTool({
		name: "nl_cockpit",
		label: "next-layer cockpit",
		description:
			"Drive a real window on its own Hyprland workspace with real pointer and keyboard input, and capture it. Coordinates are pixels inside the app window; 'ocr' and 'find-text' locate controls by their text.",
		promptSnippet: "Drive and capture the app window with real input (nl_cockpit)",
		promptGuidelines: [
			"Use nl_cockpit for anything interactive or visual: it is the only path with real input, and its shots isolate the app's own workspace.",
			"Use nl_cockpit ocr to find a control's coordinates before clicking; do not guess window geometry.",
		],
		parameters: cockpitSchema,
		executionMode: "sequential",
		async execute(_id, params, signal, _onUpdate, ctx) {
			const args = params.app ? ["--app", params.app, params.command] : [params.command];
			args.push(...(params.args ?? []));
			const result = await exec(`${REPO}/tools/cockpit/cockpit`, args, { signal, timeoutMs: 180_000 });
			const output = `${result.stdout}${result.stderr}`.trim();
			const text = output || `cockpit ${params.command}: exit ${result.code}`;
			const capture = params.command === "shot" ? (params.args ?? [])[0] : undefined;
			const image = capture ? await imageBlock(ctx, capture) : null;
			const note = capture && !image && ctx.model?.input?.includes("image") ? `capture missing at ${capture}` : "";
			return {
				content: image
					? [{ type: "text", text }, image]
					: [{ type: "text", text: note ? `${text}\n${note}` : text }],
				details: { summary: `cockpit ${params.command}: exit ${result.code}` },
				isError: result.code !== 0,
			};
		},
	});
}
