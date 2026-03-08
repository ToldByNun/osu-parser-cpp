#ifndef BEATMAPPARSER_HPP
#define BEATMAPPARSER_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class Beatmap {
public:
    struct TimingPoint {
        int32_t time;
        float beatLength;
        int32_t meter;
        int32_t sampleSet;
        int32_t sampleIndex;
        int32_t volume;
        bool uninherited;
        int32_t effects;

        void parse(const std::string& line);
    };
    struct TimingPoints {
        std::vector<TimingPoint> points;

        void parse(const std::string& blockContent);
    };
    struct Difficulty;

    struct HitObject {
        struct Point {
            int32_t x;
            int32_t y;
        };

        int32_t rawX;
        int32_t rawY;
        int32_t x;
        int32_t y;
        int32_t timeToHit;
        int32_t endTimeToHit;
        int32_t endX;
        int32_t endY;
        bool isSlider;
        int stackIndex;
        int32_t sliderRepeats;
        float sliderPixelLength;
        std::string sliderCurveType;
        std::vector<Point> sliderPoints;

        void parse(
            const std::string& line,
            int scaleWidth,
            int scaleHeight,
            int previousX,
            int previousY,
            int previousStack,
            const Difficulty& difficulty,
            const TimingPoints& timingPoints
        );
    };
    struct HitObjects {
        std::vector<HitObject> objects;

        void parse(
            const std::string& blockContent,
            int scaleWidth,
            int scaleHeight,
            const Difficulty& difficulty,
            const TimingPoints& timingPoints
        );
    };

    struct Difficulty {
        float circleSize;
        float overallDifficulty;
        float approachRate;
        float sliderMultiplier;
        float sliderTickRate;

        void parse(const std::string& blockContent);
    };

public:
    
    Difficulty difficulty;
    TimingPoints timingPoints;
    HitObjects hitObjects;

    //parser.difficulty.circleSize

    std::string fileName;

    void parse(const std::filesystem::path& path, int scaleWidth, int scaleHeight);

    // Add the hashCode method
    int32_t hashCode() const {
        std::hash<std::string> hasher;
        return static_cast<int32_t>(hasher(fileName));
    }
};

class BeatmapParser {
public:
    Beatmap currentBeatmap;
    uint32_t currentBeatmapId = 0;

    bool isParsed = false;
    bool isFound = false;

    bool parse(const std::filesystem::path& path, int scaleWidth = 1920, int scaleHeight = 1080);
    bool parseAll(const std::filesystem::path& rootFolder, int scaleWidth = 1920, int scaleHeight = 1080);
    bool parseByIds(
        const std::filesystem::path& gameFolder,
        int32_t setId,
        int32_t mapId,
        int scaleWidth = 1920,
        int scaleHeight = 1080
    );

    std::filesystem::path findMapFolder(const std::filesystem::path& gameFolder, int32_t setId) const;
    std::filesystem::path getCurrentMapPath(const std::filesystem::path& folderPath, int32_t mapId) const;
};

#endif // BEATMAPPARSER_HPP
