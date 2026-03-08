#include "BeatmapParser.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::vector<std::string> split(const std::string& input, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(input);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.emplace_back(token);
    }
    return tokens;
}

std::string trim(const std::string& value) {
    const size_t start = value.find_first_not_of(" \t\r");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = value.find_last_not_of(" \t\r");
    return value.substr(start, end - start + 1);
}

std::string getSectionContent(const std::string& fileContent, const std::string& sectionName) {
    const std::string sectionHeader = "[" + sectionName + "]";
    const size_t headerPos = fileContent.find(sectionHeader);
    if (headerPos == std::string::npos) {
        return "";
    }

    const size_t sectionStartLineEnd = fileContent.find('\n', headerPos);
    if (sectionStartLineEnd == std::string::npos) {
        return "";
    }

    const size_t sectionStart = sectionStartLineEnd + 1;
    const size_t sectionEnd = fileContent.find("\n[", sectionStart);
    if (sectionEnd == std::string::npos) {
        return fileContent.substr(sectionStart);
    }

    return fileContent.substr(sectionStart, sectionEnd - sectionStart);
}

bool tryParseOsuPoint(const std::string& token, int32_t& outX, int32_t& outY) {
    const size_t separatorPos = token.find(':');
    if (separatorPos == std::string::npos) {
        return false;
    }

    try {
        outX = std::stoi(token.substr(0, separatorPos));
        outY = std::stoi(token.substr(separatorPos + 1));
        return true;
    } catch (...) {
        return false;
    }
}

Beatmap::HitObject::Point toScaledPoint(
    int32_t osuX,
    int32_t osuY,
    float offsetX,
    float offsetY,
    float osuScale,
    float osuStackOffset
) {
    Beatmap::HitObject::Point point{};
    point.x = static_cast<int32_t>(offsetX + static_cast<float>(osuX) * osuScale + osuStackOffset);
    point.y = static_cast<int32_t>(offsetY + static_cast<float>(osuY) * osuScale + osuStackOffset) + 17;
    return point;
}

struct TimingState {
    float beatLength = 0.0f;
    float sliderVelocityMultiplier = 1.0f;
    bool hasUninheritedPoint = false;
};

TimingState getTimingStateAt(const Beatmap::TimingPoints& timingPoints, int32_t objectTime) {
    TimingState state{};

    for (const auto& point : timingPoints.points) {
        if (point.time > objectTime) {
            break;
        }

        if (point.uninherited) {
            state.beatLength = point.beatLength;
            state.hasUninheritedPoint = true;
            state.sliderVelocityMultiplier = 1.0f;
            continue;
        }

        if (point.beatLength == 0.0f) {
            continue;
        }

        const float sv = -100.0f / point.beatLength;
        if (std::isfinite(sv) && sv > 0.0f) {
            state.sliderVelocityMultiplier = sv;
        }
    }

    if (!state.hasUninheritedPoint) {
        for (const auto& point : timingPoints.points) {
            if (!point.uninherited) {
                continue;
            }

            state.beatLength = point.beatLength;
            state.hasUninheritedPoint = true;
            break;
        }
    }

    if (state.sliderVelocityMultiplier <= 0.0f || !std::isfinite(state.sliderVelocityMultiplier)) {
        state.sliderVelocityMultiplier = 1.0f;
    }

    return state;
}

int32_t computeSliderDurationMs(
    float sliderPixelLength,
    int32_t repeats,
    float sliderMultiplier,
    const TimingState& timingState
) {
    if (!timingState.hasUninheritedPoint || timingState.beatLength <= 0.0f) {
        return 0;
    }

    if (sliderPixelLength <= 0.0f || repeats <= 0 || sliderMultiplier <= 0.0f) {
        return 0;
    }

    const float spanDuration =
        (sliderPixelLength / (100.0f * sliderMultiplier * timingState.sliderVelocityMultiplier)) * timingState.beatLength;
    if (!std::isfinite(spanDuration) || spanDuration < 0.0f) {
        return 0;
    }

    const float totalDuration = spanDuration * static_cast<float>(repeats);
    if (!std::isfinite(totalDuration) || totalDuration < 0.0f) {
        return 0;
    }

    return static_cast<int32_t>(std::lround(totalDuration));
}
} // namespace

void Beatmap::TimingPoint::parse(const std::string& line) {
    const std::vector<std::string> vars = split(trim(line), ',');
    if (vars.size() < 8) {
        assert(false);
        return;
    }

    this->time = std::stoi(trim(vars[0]));
    this->beatLength = std::stof(trim(vars[1]));
    this->meter = std::stoi(trim(vars[2]));
    this->sampleSet = std::stoi(trim(vars[3]));
    this->sampleIndex = std::stoi(trim(vars[4]));
    this->volume = std::stoi(trim(vars[5]));
    this->uninherited = static_cast<bool>(std::stoi(trim(vars[6])));
    this->effects = std::stoi(trim(vars[7]));
}

void Beatmap::TimingPoints::parse(const std::string& blockContent) {
    std::stringstream ss;
    ss << blockContent;

    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        this->points.emplace_back().parse(line);
    }
}

void Beatmap::HitObject::parse(
    const std::string& line,
    int scaleWidth,
    int scaleHeight,
    int previousX,
    int previousY,
    int previousStack,
    const Difficulty& difficulty,
    const TimingPoints& timingPoints
) {
    const std::vector<std::string> vars = split(trim(line), ',');
    if (vars.size() < 4) {
        assert(false);
        return;
    }

    const float playfieldHeight = scaleHeight * 0.8f;
    const float playfieldWidth = playfieldHeight * (4.0f / 3.0f);
    const float osuScale = playfieldWidth / 512.0f;

    const float offsetX = (scaleWidth - playfieldWidth) / 2.0f;
    const float offsetY = (scaleHeight - playfieldHeight) / 2.0f;

    const int parsedX = std::stoi(trim(vars[0]));
    const int parsedY = std::stoi(trim(vars[1]));
    this->rawX = parsedX;
    this->rawY = parsedY;

    const int stackOffset = (previousX == parsedX && previousY == parsedY) ? previousStack + 1 : 0;
    const float osuStackOffset = -stackOffset * 6 * osuScale;

    const Point scaledStart = toScaledPoint(parsedX, parsedY, offsetX, offsetY, osuScale, osuStackOffset);
    this->x = scaledStart.x;
    this->y = scaledStart.y;

    this->timeToHit = std::stoi(trim(vars[2]));
    this->endTimeToHit = this->timeToHit;
    this->endX = this->x;
    this->endY = this->y;
    this->sliderRepeats = 0;
    this->sliderPixelLength = 0.0f;
    this->sliderCurveType.clear();
    this->sliderPoints.clear();

    const int objectType = std::stoi(trim(vars[3]));
    this->isSlider = (objectType & 2) != 0;
    this->stackIndex = stackOffset;

    if (!this->isSlider) {
        return;
    }

    if (vars.size() > 5) {
        const std::vector<std::string> sliderTokens = split(trim(vars[5]), '|');
        size_t pointStartIndex = 0;

        if (!sliderTokens.empty()) {
            if (sliderTokens[0].find(':') == std::string::npos) {
                this->sliderCurveType = trim(sliderTokens[0]);
                pointStartIndex = 1;
            } else {
                this->sliderCurveType = "L";
            }
        } else {
            this->sliderCurveType = "L";
        }

        this->sliderPoints.emplace_back(scaledStart);
        for (size_t i = pointStartIndex; i < sliderTokens.size(); ++i) {
            int32_t pointX = 0;
            int32_t pointY = 0;
            if (!tryParseOsuPoint(trim(sliderTokens[i]), pointX, pointY)) {
                continue;
            }

            this->sliderPoints.emplace_back(toScaledPoint(pointX, pointY, offsetX, offsetY, osuScale, osuStackOffset));
        }
    } else {
        this->sliderCurveType = "L";
        this->sliderPoints.emplace_back(scaledStart);
    }

    if (vars.size() > 6) {
        this->sliderRepeats = std::max(1, std::stoi(trim(vars[6])));
    } else {
        this->sliderRepeats = 1;
    }

    if (vars.size() > 7) {
        this->sliderPixelLength = std::stof(trim(vars[7]));
    }

    if (!this->sliderPoints.empty()) {
        const Point baseEndPoint = this->sliderPoints.back();
        const bool endsAtStart = (this->sliderRepeats % 2 == 0);
        this->endX = endsAtStart ? this->x : baseEndPoint.x;
        this->endY = endsAtStart ? this->y : baseEndPoint.y;
    }

    const TimingState timingState = getTimingStateAt(timingPoints, this->timeToHit);
    const int32_t durationMs = computeSliderDurationMs(
        this->sliderPixelLength,
        this->sliderRepeats,
        difficulty.sliderMultiplier,
        timingState
    );
    this->endTimeToHit = this->timeToHit + durationMs;
}

void Beatmap::HitObjects::parse(
    const std::string& blockContent,
    int scaleWidth,
    int scaleHeight,
    const Difficulty& difficulty,
    const TimingPoints& timingPoints
) {
    std::stringstream ss(blockContent);
    std::string line;

    int lastX = -1;
    int lastY = -1;
    int lastStack = 0;

    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        this->objects.emplace_back();
        HitObject& obj = this->objects.back();

        obj.parse(line, scaleWidth, scaleHeight, lastX, lastY, lastStack, difficulty, timingPoints);

        lastX = obj.rawX;
        lastY = obj.rawY;
        lastStack = obj.stackIndex;
    }
}

void Beatmap::Difficulty::parse(const std::string& blockContent) {
    const std::vector<std::string> varsToRead = {
        "CircleSize",
        "OverallDifficulty",
        "ApproachRate",
        "SliderMultiplier",
        "SliderTickRate"
    };

    for (const auto& var : varsToRead) {
        size_t varPos = blockContent.find(var);
        if (varPos == std::string::npos) {
            assert(false);
            return;
        }

        varPos += var.size() + 1; // +1 to skip :
        const float varVal = std::stof(blockContent.substr(varPos, blockContent.find("\n", varPos) - varPos));

        if (var == "CircleSize") {
            this->circleSize = varVal;
        }

        if (var == "OverallDifficulty") {
            this->overallDifficulty = varVal;
        }

        if (var == "ApproachRate") {
            this->approachRate = varVal;
        }

        if (var == "SliderMultiplier") {
            this->sliderMultiplier = varVal;
        }

        if (var == "SliderTickRate") {
            this->sliderTickRate = varVal;
        }
    }
}

void Beatmap::parse(const std::filesystem::path& path, int scaleWidth, int scaleHeight) {
    this->fileName = path.filename().string();
    this->timingPoints.points.clear();
    this->hitObjects.objects.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        assert(false);
        return;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    file.close();
    const std::string fileContent = ss.str();

    const std::string difficultyBlock = getSectionContent(fileContent, "Difficulty");
    const std::string timingPointsBlock = getSectionContent(fileContent, "TimingPoints");
    const std::string hitObjectsBlock = getSectionContent(fileContent, "HitObjects");

    if (difficultyBlock.empty() || timingPointsBlock.empty() || hitObjectsBlock.empty()) {
        assert(false);
        return;
    }

    this->difficulty.parse(difficultyBlock);
    this->timingPoints.parse(timingPointsBlock);
    this->hitObjects.parse(hitObjectsBlock, scaleWidth, scaleHeight, this->difficulty, this->timingPoints);
}

bool BeatmapParser::parse(const std::filesystem::path& path, int scaleWidth, int scaleHeight) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        return false;
    }

    this->currentBeatmap.parse(path, scaleWidth, scaleHeight);
    this->isParsed = true;
    return true;
}

bool BeatmapParser::parseAll(const std::filesystem::path& rootFolder, int scaleWidth, int scaleHeight) {
    if (!std::filesystem::exists(rootFolder) || !std::filesystem::is_directory(rootFolder)) {
        return false;
    }

    bool parsedAny = false;
    for (const auto& file : std::filesystem::recursive_directory_iterator(rootFolder)) {
        const std::filesystem::path& path = file.path();
        if (!file.is_regular_file() || path.extension() != ".osu") {
            continue;
        }

        if (this->parse(path, scaleWidth, scaleHeight)) {
            parsedAny = true;
        }
    }

    this->isFound = parsedAny;
    return parsedAny;
}

bool BeatmapParser::parseByIds(
    const std::filesystem::path& gameFolder,
    int32_t setId,
    int32_t mapId,
    int scaleWidth,
    int scaleHeight
) {
    const std::filesystem::path songsFolder = this->findMapFolder(gameFolder, setId);
    if (songsFolder.empty()) {
        this->isFound = false;
        return false;
    }

    const std::filesystem::path mapPath = this->getCurrentMapPath(songsFolder, mapId);
    if (mapPath.empty()) {
        this->isFound = false;
        return false;
    }

    if (!this->parse(mapPath, scaleWidth, scaleHeight)) {
        this->isFound = false;
        return false;
    }

    this->isFound = true;
    this->currentBeatmapId = static_cast<uint32_t>(mapId);
    return true;
}

std::filesystem::path BeatmapParser::findMapFolder(const std::filesystem::path& gameFolder, int32_t setId) const {
    const std::filesystem::path songsFolder = gameFolder / "Songs";
    if (!std::filesystem::exists(songsFolder) || !std::filesystem::is_directory(songsFolder)) {
        return {};
    }

    const std::string setIdStr = std::to_string(setId);
    for (const auto& entry : std::filesystem::directory_iterator(songsFolder)) {
        if (!entry.is_directory()) {
            continue;
        }

        const std::string folderName = entry.path().filename().string();
        if (folderName.rfind(setIdStr, 0) != 0) { // starts with setId
            continue;
        }

        return entry.path();
    }

    return {};
}

std::filesystem::path BeatmapParser::getCurrentMapPath(const std::filesystem::path& folderPath, int32_t mapId) const {
    if (!std::filesystem::exists(folderPath) || !std::filesystem::is_directory(folderPath)) {
        return {};
    }

    const std::string mapIdStr = std::to_string(mapId);
    for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::filesystem::path& filePath = entry.path();
        if (filePath.extension() != ".osu") {
            continue;
        }

        std::ifstream file(filePath);
        if (!file.is_open()) {
            continue;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.rfind("BeatmapID:", 0) != 0) {
                continue;
            }

            std::string value = line.substr(10); // skip "BeatmapID:"
            value.erase(0, value.find_first_not_of(" \t")); // trim left whitespace

            if (value == mapIdStr) {
                return filePath;
            }
        }
    }

    return {};
}
