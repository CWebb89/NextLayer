#include <Slic3r/App/CLI/CLIApp.hpp>
#include <Slic3r/App/Init.hpp>

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 4 && argc != 5) {
        std::fprintf(stderr, "usage: %s MODEL CONFIG.json OUT.gcode\n", argv[0]);
        return 2;
    }

    Slic3r::App::init_common();

    Slic3r::App::InitParams params;
    params.input.input_files.push_back(argv[1]);
    params.input.config_file   = argv[2];
    params.action.export_gcode = true;
    params.misc.output         = argv[3];

    Slic3r::App::init_paths(params);

    const int first = Slic3r::App::CLI::run(params);
    if (argc == 5 && std::string(argv[4]) == "--twice") {
        std::fprintf(stderr, "first run returned %d, repeating\n", first);
        Slic3r::App::InitParams again;
        again.input.input_files.push_back(argv[1]);
        again.input.config_file   = argv[2];
        again.action.export_gcode = true;
        again.misc.output         = std::string(argv[3]) + ".2";
        const int second = Slic3r::App::CLI::run(again);
        std::fprintf(stderr, "second run returned %d\n", second);
        return second;
    }
    return first;
}
