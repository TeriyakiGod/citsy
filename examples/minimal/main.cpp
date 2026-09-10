// minimal example: load a .bitsy file with MockHost and print parse results.
//
// Usage: ./minimal_example path/to/game.bitsy

#include <citsy/engine.hpp>
#include <citsy/types.hpp>
#include "backends/mock/mock_host.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <game.bitsy>\n";
        return EXIT_FAILURE;
    }

    const std::string path = argv[1];

    try {
        citsy::Engine engine = citsy::Engine::from_file(path);
        citsy::MockHost host;

        engine.start(host);
        std::cout << "Engine started. Running 3 frames...\n";

        for (int frame = 0; frame < 3; ++frame) {
            engine.update(host);
        }

        const auto* snap = host.last_snapshot();
        if (snap) {
            std::cout << "Last frame palette size : " << snap->palette.size() << "\n";
            std::cout << "Map1 buffer size        : " << snap->map1.size() << "\n";
            std::cout << "Video buffer size       : " << snap->video.size() << "\n";
            std::cout << "Textbox visible         : " << std::boolalpha << snap->textbox_visible << "\n";
            if (!snap->palette.empty()) {
                const auto& bg = snap->palette[0];
                std::cout << "Background color        : rgb("
                          << static_cast<int>(bg.r) << ","
                          << static_cast<int>(bg.g) << ","
                          << static_cast<int>(bg.b) << ")\n";
            }
        }

        std::cout << "Done. Total frames presented: " << host.snapshots.size() << "\n";
        return EXIT_SUCCESS;

    } catch (const citsy::ParseError& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (const std::ios_base::failure& e) {
        std::cerr << "I/O error: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
