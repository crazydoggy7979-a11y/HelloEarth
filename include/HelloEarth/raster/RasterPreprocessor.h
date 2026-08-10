#pragma once

#include <string>

namespace HelloEarth::Raster
{
    bool prepareRasterForLoading(
        const std::string& imagePath,
        std::string& preparedImagePath
    );
}