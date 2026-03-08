#include "BeatmapParser/BeatmapParser.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {
std::filesystem::path getDefaultMapPath() {
    const std::filesystem::path mapsDir = "Maps";
    if (!std::filesystem::exists(mapsDir) || !std::filesystem::is_directory(mapsDir)) {
        return {};
    }

    for (const auto& entry : std::filesystem::directory_iterator(mapsDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".osu") {
            continue;
        }
        return entry.path();
    }

    return {};
}
} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path mapPath;
    if (argc >= 2) {
        mapPath = argv[1];
    } else {
        mapPath = getDefaultMapPath();
    }

    if (mapPath.empty()) {
        std::cerr << "Usage: ParserUpgrade <path-to-map.osu>\n";
        std::cerr << "No default .osu file found in Maps/.\n";
        return 1;
    }

    BeatmapParser parser;
    if (!parser.parse(mapPath)) {
        std::cerr << "Failed to parse beatmap: " << mapPath.string() << "\n";
        return 1;
    }

    std::cout << "Parsed beatmap: " << parser.currentBeatmap.fileName << "\n";
    std::cout << "HitObjects: " << parser.currentBeatmap.hitObjects.objects.size() << "\n\n";

    std::cout << "Slider output:\n";
    int sliderIndex = 0;
    for (const auto& obj : parser.currentBeatmap.hitObjects.objects) {
        if (!obj.isSlider) {
            continue;
        }

        ++sliderIndex;
        std::cout << "Slider #" << sliderIndex
                  << " | time-range: " << obj.timeToHit << " -> " << obj.endTimeToHit
                  << " | position-range: (" << obj.x << ", " << obj.y << ") -> ("
                  << obj.endX << ", " << obj.endY << ")\n";
        std::cout << "  curve=" << obj.sliderCurveType
                  << " repeats=" << obj.sliderRepeats
                  << " length=" << obj.sliderPixelLength
                  << " points=" << obj.sliderPoints.size() << "\n";
        for (size_t i = 0; i < obj.sliderPoints.size(); ++i) {
            std::cout << "    p" << i << ": (" << obj.sliderPoints[i].x << ", " << obj.sliderPoints[i].y << ")\n";
        }
    }

    if (sliderIndex == 0) {
        std::cout << "No sliders in map.\n";
    }

    return 0;
}
