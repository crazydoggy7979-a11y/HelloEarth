#include <osgEarth/MapNode>
#include <osgEarth/TMS>
#include <osgEarth/EarthManipulator>
#include <osgEarth/GLUtils>
#include <osgEarth/GeoData>
#include <osgEarth/Units>
#include <osgEarth/Viewpoint>

#include <osg/ArgumentParser>
#include <osgViewer/Viewer>
#include <osgEarth/GDAL>
#include <iostream>
#include <algorithm>
#include <gdal_priv.h>
#include <filesystem>
#include <vector>
#include <cpl_progress.h>
#include <gdal_utils.h>
#include <cpl_string.h>

struct OverviewLevelSize{
    int width;
    int height;
};

bool collectOverviewLevelSizes(
    GDALRasterBand* rasterBand,
    std::vector<OverviewLevelSize>& levelSizes){
    if (rasterBand == nullptr)
    {
        return false;
    }

    levelSizes.clear();

    // 直接打开 .ovr 时，波段自身就是原始 TIFF 的第一层 Overview。
    levelSizes.push_back(
        {
            rasterBand->GetXSize(),
            rasterBand->GetYSize()
        }
    );

    const int nestedOverviewCount =
        rasterBand->GetOverviewCount();

    for (int overviewIndex = 0;
         overviewIndex < nestedOverviewCount;
         ++overviewIndex)
    {
        GDALRasterBand* overviewBand =
            rasterBand->GetOverview(overviewIndex);

        if (overviewBand == nullptr)
        {
            return false;
        }

        levelSizes.push_back(
            {
                overviewBand->GetXSize(),
                overviewBand->GetYSize()
            }
        );
    }

    return true;
}

std::vector<int> calculateOverviewFactors(
    int rasterWidth,
    int rasterHeight,
    int smallestOverviewSize){
    std::vector<int> overviewFactors;

    if (rasterWidth <= smallestOverviewSize &&
        rasterHeight <= smallestOverviewSize)
    {
        return overviewFactors;
    }

    int factor = 2;

    while (true)
    {
        overviewFactors.push_back(factor);

        // 使用向上取整，处理不能被缩放因子整除的尺寸。
        const int overviewWidth =
            (rasterWidth + factor - 1) / factor;

        const int overviewHeight =
            (rasterHeight + factor - 1) / factor;

        if (overviewWidth <= smallestOverviewSize &&
            overviewHeight <= smallestOverviewSize)
        {
            break;
        }

        factor *= 2;
    }

    return overviewFactors;
}

std::filesystem::path makeOverviewBackupPath(
    const std::filesystem::path& externalOverviewPath){
    std::filesystem::path backupPath =
        externalOverviewPath;

    backupPath += ".invalid.bak";

    int backupIndex = 1;

    while (std::filesystem::exists(backupPath))
    {
        backupPath = externalOverviewPath;

        backupPath +=
            ".invalid.bak." +
            std::to_string(backupIndex);

        ++backupIndex;
    }

    return backupPath;
}

bool buildExternalOverview(
    const std::string& imagePath,
    const std::vector<int>& overviewFactors){
    if (overviewFactors.empty())
    {
        std::cerr
            << "No overview factors were provided."
            << std::endl;

        return false;
    }

    // 使用只读模式打开 GeoTIFF。
    // 对只读 GeoTIFF 调用 BuildOverviews，
    // GDAL 会创建外部 image.tif.ovr，
    // 而不是修改 TIFF 内部。
    GDALDataset* buildDataset =
        static_cast<GDALDataset*>(
            GDALOpenEx(
                imagePath.c_str(),
                GDAL_OF_RASTER | GDAL_OF_READONLY,
                nullptr,
                nullptr,
                nullptr
            )
        );

    if (buildDataset == nullptr)
    {
        std::cerr
            << "Failed to reopen the TIFF for "
            << "external overview creation."
            << std::endl;

        return false;
    }

    std::cout
        << "Building external overviews..."
        << std::endl;

    const CPLErr buildResult =
        buildDataset->BuildOverviews(
            "AVERAGE",

            static_cast<int>(
                overviewFactors.size()
            ),

            overviewFactors.data(),

            // 0 表示为数据集的全部波段构建。
            0,

            // 因为上一项是 0，所以不需要提供波段号数组。
            nullptr,

            // 在终端中输出构建进度。
            GDALTermProgress,

            // 不需要额外传递进度回调数据。
            nullptr,

            // 暂时不设置额外的 Overview 创建选项。
            nullptr
        );

    GDALClose(buildDataset);
    buildDataset = nullptr;

    if (buildResult != CE_None)
    {
        std::cerr
            << "Failed to build external overviews."
            << std::endl;

        return false;
    }

    std::cout
        << "External overviews were built successfully."
        << std::endl;

    return true;
}

bool checkInfoImage(const std::string& imagePath, int& rasterBandCount, int& rasterWidth, int& rasterHeight){
    // 在 osgEarth 创建影像图层之前，先由 GDAL 以只读方式打开影像，
    // 为后续检查尺寸和 Overview 做准备。
    GDALDataset* rasterDataset =
    static_cast<GDALDataset*>(
        GDALOpenEx(
            imagePath.c_str(),
            GDAL_OF_RASTER | GDAL_OF_READONLY,
            nullptr,
            nullptr,
            nullptr
        )
    );

    // GDALOpenEx 打开失败时返回 nullptr。
    if (rasterDataset == nullptr){
        std::cerr
            << "GDAL failed to open raster: "
            << imagePath
            << std::endl;

        return false;
    }

    // 获取栅格数据集中的波段数量。
    rasterBandCount = rasterDataset->GetRasterCount();

    // 普通影像至少应该包含一个栅格波段。
    if (rasterBandCount <= 0){
        std::cerr
            << "The raster dataset contains no raster bands."
            << std::endl;

        GDALClose(rasterDataset);
        return false;
    }

    // 获取 Dataset 的公共栅格尺寸。
    // 按照 GDAL Raster Data Model，所有全分辨率波段
    // 都应该使用这一组公共宽高。
    rasterWidth = rasterDataset->GetRasterXSize();

    rasterHeight = rasterDataset->GetRasterYSize();

    // 宽度或高度小于等于 0 时，无法作为正常栅格继续处理。
    if (rasterWidth <= 0 || rasterHeight <= 0){
        std::cerr
            << "The raster dataset has an invalid size."
            << std::endl;

        GDALClose(rasterDataset);
        return false;
    }

    for (int bandIndex=1; bandIndex <= rasterBandCount; bandIndex++){
        GDALRasterBand* rasterBand = rasterDataset->GetRasterBand(bandIndex);
        if(rasterBand == nullptr){
            std::cerr
                << "Failed to access raster band "
                << bandIndex
                << "."
                << std::endl;

            GDALClose(rasterDataset);
            return false;
        }
        const int bandWidth = rasterBand->GetXSize();
        const int bandHeight = rasterBand->GetYSize();
        
        // 当前 SingleLocalTIF 示例不处理尺寸不同的独立影像、
        // Subdataset 或其他复杂栅格组织形式。
        if(bandWidth != rasterWidth || bandHeight != rasterHeight){
            std::cerr
                << "Raster band "
                << bandIndex
                << " does not match the dataset size."
                << std::endl;

            GDALClose(rasterDataset);
            return false;
        }
    }
    GDALClose(rasterDataset);
    rasterDataset = nullptr;
    return true;
}

bool checkOverviewImage(const std::string& overviewPath, int rasterBandCount, int rasterWidth, int rasterHeight, int smallestOverviewSize){   
    if (smallestOverviewSize <= 0)
    {
        std::cerr
            << "The smallest overview size must be positive."
            << std::endl;

        return false;
    }
    
    const std::string externalOverviewPathString = overviewPath;

    GDALDataset* externalOverviewDataset =
        static_cast<GDALDataset*>(
            GDALOpenEx(
                externalOverviewPathString.c_str(),
                GDAL_OF_RASTER | GDAL_OF_READONLY,
                nullptr,
                nullptr,
                nullptr
            )
        );

    if (externalOverviewDataset == nullptr)
    {
        std::cerr
            << "The external overview file exists, "
            << "but GDAL failed to open it."
            << std::endl;
        return false;
    }
    
    const int externalOverviewBandCount =
        externalOverviewDataset->GetRasterCount();

    if (externalOverviewBandCount != rasterBandCount)
    {
        std::cerr
            << "External overview band count mismatch. "
            << "TIFF bands: "
            << rasterBandCount
            << ", overview bands: "
            << externalOverviewBandCount
            << "."
            << std::endl;
        GDALClose(externalOverviewDataset);
        externalOverviewDataset = nullptr;
        return false;
    }

    std::vector<OverviewLevelSize> referenceLevelSizes;
    GDALRasterBand* referenceBand = externalOverviewDataset->GetRasterBand(1);
    if(!collectOverviewLevelSizes(referenceBand, referenceLevelSizes)){
        std::cerr
            << "Failed to collect overview levels "
            << "from reference band 1."
            << std::endl;
        GDALClose(externalOverviewDataset);
        externalOverviewDataset = nullptr;
        return false;
    }
    else{
        int previousWidth = rasterWidth;
        int previousHeight = rasterHeight;

        for (std::size_t levelIndex = 0;
            levelIndex < referenceLevelSizes.size();
            ++levelIndex){
            const OverviewLevelSize& currentLevel =
                referenceLevelSizes[levelIndex];

            const bool hasInvalidSize =
                currentLevel.width <= 0 ||
                currentLevel.height <= 0;

            const bool becomesLarger =
                currentLevel.width > previousWidth ||
                currentLevel.height > previousHeight;

            const bool doesNotShrink =
                currentLevel.width == previousWidth &&
                currentLevel.height == previousHeight;

            if (hasInvalidSize ||
                becomesLarger ||
                doesNotShrink)
            {
                std::cerr
                    << "Invalid reference overview level "
                    << levelIndex
                    << ": "
                    << currentLevel.width
                    << " x "
                    << currentLevel.height
                    << "."
                    << std::endl;
                GDALClose(externalOverviewDataset);
                externalOverviewDataset = nullptr;
                return false;
            }

            previousWidth = currentLevel.width;
            previousHeight = currentLevel.height;
        }
    }

    for (int bandIndex = 2;
        bandIndex <= externalOverviewBandCount;
        ++bandIndex)
    {
        GDALRasterBand* currentBand =
            externalOverviewDataset->GetRasterBand(bandIndex);

        std::vector<OverviewLevelSize> currentLevelSizes;

        if (!collectOverviewLevelSizes(
                currentBand,
                currentLevelSizes))
        {
            std::cerr
                << "Failed to collect overview levels from band "
                << bandIndex
                << "."
                << std::endl;
            GDALClose(externalOverviewDataset);
            externalOverviewDataset = nullptr;
            return false;
        }

        if (currentLevelSizes.size() !=
            referenceLevelSizes.size())
        {
            std::cerr
                << "Overview level count mismatch on band "
                << bandIndex
                << "."
                << std::endl;
            GDALClose(externalOverviewDataset);
            externalOverviewDataset = nullptr;
            return false;
        }

        for (std::size_t levelIndex = 0;
            levelIndex < referenceLevelSizes.size();
            ++levelIndex)
        {
            const OverviewLevelSize& referenceLevel =
                referenceLevelSizes[levelIndex];

            const OverviewLevelSize& currentLevel =
                currentLevelSizes[levelIndex];

            if (currentLevel.width != referenceLevel.width ||
                currentLevel.height != referenceLevel.height)
            {
                std::cerr
                    << "Overview size mismatch on band "
                    << bandIndex
                    << ", level "
                    << levelIndex
                    << ". Expected "
                    << referenceLevel.width
                    << " x "
                    << referenceLevel.height
                    << ", but found "
                    << currentLevel.width
                    << " x "
                    << currentLevel.height
                    << "."
                    << std::endl;
                GDALClose(externalOverviewDataset);
                externalOverviewDataset = nullptr;  
                return false;
            }
        }
    }
    
    if (referenceLevelSizes.empty())
    {
        std::cerr
            << "The overview contains no valid levels."
            << std::endl;

        GDALClose(externalOverviewDataset);
        externalOverviewDataset = nullptr;

        return false;
    }

    const OverviewLevelSize& smallestLevel =
        referenceLevelSizes.back();

    if (smallestLevel.width > smallestOverviewSize ||
        smallestLevel.height > smallestOverviewSize)
    {
        std::cerr
            << "The overview pyramid is incomplete. "
            << "Its smallest level is still "
            << smallestLevel.width
            << " x "
            << smallestLevel.height
            << ", but both dimensions are expected "
            << "to be no greater than "
            << smallestOverviewSize
            << "."
            << std::endl;

        GDALClose(externalOverviewDataset);
        externalOverviewDataset = nullptr;

        return false;
    }

    GDALClose(externalOverviewDataset);
    externalOverviewDataset = nullptr;
    return true;
}

bool hasInternalOverview(const std::string& imagePath, int rasterBandCount, bool& internalOverviewExists){
    internalOverviewExists = false;

    GDALDataset* rasterDataset =
        static_cast<GDALDataset*>(
            GDALOpenEx(
                imagePath.c_str(),
                GDAL_OF_RASTER | GDAL_OF_READONLY,
                nullptr,
                nullptr,
                nullptr
            )
        );

    if (rasterDataset == nullptr)
    {
        std::cerr
            << "Failed to reopen the TIFF "
            << "for internal overview inspection."
            << std::endl;

        return false;
    }

    for (int bandIndex = 1;
         bandIndex <= rasterBandCount;
         ++bandIndex)
    {
        GDALRasterBand* rasterBand =
            rasterDataset->GetRasterBand(bandIndex);

        if (rasterBand == nullptr)
        {
            std::cerr
                << "Failed to access raster band "
                << bandIndex
                << " while inspecting internal overviews."
                << std::endl;

            GDALClose(rasterDataset);
            return false;
        }

        if (rasterBand->GetOverviewCount() > 0)
        {
            internalOverviewExists = true;
            break;
        }
    }

    GDALClose(rasterDataset);
    rasterDataset = nullptr;

    return true;
}

bool createIsolatedVRT(const std::string& sourceImagePath, const std::string& managedVrtPath){
    GDALDatasetH sourceDataset =
        GDALOpenEx(
            sourceImagePath.c_str(),
            GDAL_OF_RASTER | GDAL_OF_READONLY,
            nullptr,
            nullptr,
            nullptr
        );

    if (sourceDataset == nullptr)
    {
        std::cerr
            << "Failed to open the source TIFF "
            << "for VRT creation."
            << std::endl;

        return false;
    }

    // 这些参数等价于：
    // gdal_translate -of VRT -ovr NONE source.tif managed.vrt
    //
    // -ovr NONE 要求 GDAL只使用源 TIFF 的全分辨率数据，
    // 不使用它已有的内部 Overview。
    char** translateArguments = nullptr;

    translateArguments =
        CSLAddString(
            translateArguments,
            "-of"
        );

    translateArguments =
        CSLAddString(
            translateArguments,
            "VRT"
        );

    translateArguments =
        CSLAddString(
            translateArguments,
            "-ovr"
        );

    translateArguments =
        CSLAddString(
            translateArguments,
            "NONE"
        );

    GDALTranslateOptions* translateOptions =
        GDALTranslateOptionsNew(
            translateArguments,
            nullptr
        );

    CSLDestroy(translateArguments);
    translateArguments = nullptr;

    if (translateOptions == nullptr)
    {
        std::cerr
            << "Failed to create GDAL VRT translation options."
            << std::endl;

        GDALClose(sourceDataset);
        return false;
    }

    int usageError = FALSE;

    GDALDatasetH managedVrtDataset =
        GDALTranslate(
            managedVrtPath.c_str(),
            sourceDataset,
            translateOptions,
            &usageError
        );

    GDALTranslateOptionsFree(translateOptions);
    translateOptions = nullptr;

    GDALClose(sourceDataset);
    sourceDataset = nullptr;

    if (managedVrtDataset == nullptr ||
        usageError != FALSE)
    {
        std::cerr
            << "Failed to create the managed VRT."
            << std::endl;

        if (managedVrtDataset != nullptr)
        {
            GDALClose(managedVrtDataset);
        }

        return false;
    }

    GDALClose(managedVrtDataset);
    managedVrtDataset = nullptr;

    std::cout
        << "Managed VRT created: "
        << managedVrtPath
        << std::endl;

    return true;
}

bool checkIsolatedVRT(const std::string& managedVrtPath, int sourceBandCount, int sourceWidth, int sourceHeight)
{
    int vrtBandCount = 0;
    int vrtWidth = 0;
    int vrtHeight = 0;

    if (!checkInfoImage(
            managedVrtPath,
            vrtBandCount,
            vrtWidth,
            vrtHeight))
    {
        std::cerr
            << "The managed VRT failed basic raster validation."
            << std::endl;

        return false;
    }

    if (vrtBandCount != sourceBandCount ||
        vrtWidth != sourceWidth ||
        vrtHeight != sourceHeight)
    {
        std::cerr
            << "The managed VRT does not match "
            << "the source TIFF structure."
            << std::endl;

        return false;
    }

    bool vrtExposesOverview = false;

    if (!hasInternalOverview(
            managedVrtPath,
            vrtBandCount,
            vrtExposesOverview))
    {
        return false;
    }

    if (vrtExposesOverview)
    {
        std::cerr
            << "The managed VRT still exposes "
            << "source overviews."
            << std::endl;

        return false;
    }

    return true;
}

bool checkSingleImage(const std::string& imagePath, std::string& preparedImagePath){
    preparedImagePath = imagePath;
    constexpr int smallestOverviewImageSize = 1024;
    constexpr int smallestOverviewSize = 256;
    int rasterBandCount = 0;
    int rasterWidth = 0;
    int rasterHeight = 0;

    if(!checkInfoImage(imagePath, rasterBandCount, rasterWidth, rasterHeight)){
        return false;
    }

    const bool imageNeedsNewOverview =
        rasterWidth > smallestOverviewImageSize ||
        rasterHeight > smallestOverviewImageSize;
    

    const std::filesystem::path tifPath(imagePath);

    std::filesystem::path externalOverviewPath = tifPath;
    externalOverviewPath += ".ovr";

    const bool externalOverviewExists =
        std::filesystem::exists(externalOverviewPath);
    
    const std::string externalOverviewPathString =
        externalOverviewPath.string();
    
    if(externalOverviewExists){
        if(!checkOverviewImage(externalOverviewPathString, rasterBandCount, rasterWidth, rasterHeight, smallestOverviewSize)){
            const std::vector<int> overviewFactors =calculateOverviewFactors(rasterWidth, rasterHeight, smallestOverviewSize);
            const std::filesystem::path backupPath =makeOverviewBackupPath(externalOverviewPath);
            
            std::error_code fileSystemError;
            std::filesystem::rename(
                externalOverviewPath,
                backupPath,
                fileSystemError
            );

            if (fileSystemError)
            {
                std::cerr
                    << "Failed to back up the invalid overview: "
                    << fileSystemError.message()
                    << std::endl;

                return false;
            }

            if (overviewFactors.empty())
            {
                std::cout
                    << "The invalid external overview was backed up, "
                    << "but the source image is already small enough. "
                    << "No replacement overview is required."
                    << std::endl;

                return true;
            }

            if (!buildExternalOverview(imagePath, overviewFactors)){
                std::cerr
                    << "Overview rebuilding failed. "
                    << "Restoring the backup."
                    << std::endl;
                std::error_code restoreError;

                // 构建失败后可能残留一个不完整的新 .ovr。
                if (std::filesystem::exists(externalOverviewPath)){
                    std::filesystem::remove(externalOverviewPath, restoreError);
                    if (restoreError){
                        std::cerr
                            << "Failed to remove the incomplete overview: "
                            << restoreError.message()
                            << std::endl;
                        return false;
                    }
                }

                std::filesystem::rename(backupPath, externalOverviewPath, restoreError);
                if (restoreError){
                    std::cerr
                        << "Failed to restore the overview backup: "
                        << restoreError.message()
                        << std::endl;
                }
                return false;
            }
            else{
                if(!checkOverviewImage(externalOverviewPathString, rasterBandCount, rasterWidth, rasterHeight, smallestOverviewSize)){
                    std::cerr
                        << "Overview rebuilding failed. "
                        << "Restoring the backup."
                        << std::endl;
                    std::error_code restoreError;

                    // 构建失败后可能残留一个不完整的新 .ovr。
                    if (std::filesystem::exists(externalOverviewPath)){
                        std::filesystem::remove(externalOverviewPath, restoreError);
                        if (restoreError){
                            std::cerr
                                << "Failed to remove the incomplete overview: "
                                << restoreError.message()
                                << std::endl;
                            return false;
                        }
                    }

                    std::filesystem::rename(backupPath, externalOverviewPath, restoreError);
                    if (restoreError){
                        std::cerr
                            << "Failed to restore the overview backup: "
                            << restoreError.message()
                            << std::endl;
                    }
                    return false;
                }
                else{
                    std::cout
                        << "The new external overview has been created."
                        << std::endl;

                    std::cout
                        << "The backup is temporarily preserved at: "
                        << backupPath.string()
                        << std::endl;
                    
                    return true;
                }
            }
        }
        else{
            std::cout
                << "The existing external overview passed validation."
                << std::endl;

            return true;
        }
    }
    else{
        bool internalOverviewExists = false;

        if (!hasInternalOverview(
                imagePath,
                rasterBandCount,
                internalOverviewExists))
        {
            return false;
        }

        if (!internalOverviewExists)
        {
            if (!imageNeedsNewOverview)
            {
                std::cout
                    << "The image is small and has no overview. "
                    << "Overview creation is unnecessary."
                    << std::endl;

                return true;
            }

            std::cout
                << "No external or internal overview was found."
                << std::endl;

            const std::vector<int> overviewFactors =
                calculateOverviewFactors(
                    rasterWidth,
                    rasterHeight,
                    smallestOverviewSize
                );

            if (!buildExternalOverview(imagePath, overviewFactors)){
                std::cerr
                    << "External overview creation or validation failed."
                    << std::endl;
                std::error_code restoreError;

                // 构建失败后可能残留一个不完整的新 .ovr。
                if (std::filesystem::exists(externalOverviewPath)){
                    std::filesystem::remove(externalOverviewPath, restoreError);
                    if (restoreError){
                        std::cerr
                            << "Failed to remove the incomplete overview: "
                            << restoreError.message()
                            << std::endl;
                        return false;
                    }
                }
                return false;
            }
            else{
                if(!checkOverviewImage(externalOverviewPathString, rasterBandCount, rasterWidth, rasterHeight, smallestOverviewSize)){
                    std::cerr
                        << "External overview creation or validation failed."
                        << std::endl;
                    std::error_code restoreError;

                    // 构建失败后可能残留一个不完整的新 .ovr。
                    if (std::filesystem::exists(externalOverviewPath)){
                        std::filesystem::remove(externalOverviewPath, restoreError);
                        if (restoreError){
                            std::cerr
                                << "Failed to remove the incomplete overview: "
                                << restoreError.message()
                                << std::endl;
                            return false;
                        }
                    }
                    return false;
                }
                else{
                    std::cout
                        << "The new external overview has been created."
                        << std::endl;
                    
                    return true;
                }
            }
        }
        else{
            if(!checkOverviewImage(imagePath, rasterBandCount, rasterWidth+1, rasterHeight+1, smallestOverviewSize)){
                std::filesystem::path managedVrtPath =
                    std::filesystem::path(imagePath);

                managedVrtPath.replace_extension(".managed.vrt");

                std::filesystem::path managedVrtOverviewPath =
                    managedVrtPath;

                managedVrtOverviewPath += ".ovr";

                const bool managedVrtExists =
                    std::filesystem::exists(managedVrtPath);

                const bool managedVrtOverviewExists =
                    std::filesystem::exists(managedVrtOverviewPath);

                if (managedVrtExists && managedVrtOverviewExists){
                    int existingVrtBandCount = 0;
                    int existingVrtWidth = 0;
                    int existingVrtHeight = 0;

                    const bool existingVrtInfoIsValid =
                        checkInfoImage(
                            managedVrtPath.string(),
                            existingVrtBandCount,
                            existingVrtWidth,
                            existingVrtHeight
                        );

                    const bool existingVrtStructureMatches =
                        existingVrtInfoIsValid &&
                        existingVrtBandCount == rasterBandCount &&
                        existingVrtWidth == rasterWidth &&
                        existingVrtHeight == rasterHeight;

                    const bool existingVrtOverviewIsValid =
                        existingVrtStructureMatches &&
                        checkOverviewImage(
                            managedVrtOverviewPath.string(),
                            existingVrtBandCount,
                            existingVrtWidth,
                            existingVrtHeight,
                            smallestOverviewSize
                        );

                    if (existingVrtOverviewIsValid)
                    {
                        preparedImagePath =
                            managedVrtPath.string();

                        std::cout
                            << "The existing managed VRT and its overview "
                            << "passed validation and will be reused."
                            << std::endl;

                        return true;
                    }

                    std::cout
                        << "The existing managed VRT overview "
                        << "is incomplete or invalid. It will be rebuilt."
                        << std::endl;
                }

                if (std::filesystem::exists(
                        managedVrtOverviewPath))
                {
                    std::error_code removeOverviewError;

                    std::filesystem::remove(
                        managedVrtOverviewPath,
                        removeOverviewError
                    );

                    if (removeOverviewError)
                    {
                        std::cerr
                            << "Failed to remove the invalid or orphaned "
                            << "managed VRT overview: "
                            << removeOverviewError.message()
                            << std::endl;

                        return false;
                    }
                }

                bool managedVrtIsReady = false;

                if (std::filesystem::exists(managedVrtPath))
                {
                    managedVrtIsReady =
                        checkIsolatedVRT(
                            managedVrtPath.string(),
                            rasterBandCount,
                            rasterWidth,
                            rasterHeight
                        );

                    if (managedVrtIsReady)
                    {
                        std::cout
                            << "The existing managed VRT is valid. "
                            << "Only its overview needs to be rebuilt."
                            << std::endl;
                    }
                    else
                    {
                        std::error_code removeVrtError;

                        std::filesystem::remove(
                            managedVrtPath,
                            removeVrtError
                        );

                        if (removeVrtError)
                        {
                            std::cerr
                                << "Failed to remove the invalid managed VRT: "
                                << removeVrtError.message()
                                << std::endl;

                            return false;
                        }
                    }
                }

                if (!managedVrtIsReady)
                {
                    if (!createIsolatedVRT(
                            imagePath,
                            managedVrtPath.string()))
                    {
                        return false;
                    }

                    if (!checkIsolatedVRT(
                            managedVrtPath.string(),
                            rasterBandCount,
                            rasterWidth,
                            rasterHeight))
                    {
                        std::cerr
                            << "The newly created managed VRT "
                            << "failed isolation validation."
                            << std::endl;

                        return false;
                    }

                    managedVrtIsReady = true;

                    std::cout
                        << "A new isolated managed VRT was created."
                        << std::endl;
                }

                const std::vector<int> vrtOverviewFactors =
                    calculateOverviewFactors(
                        rasterWidth,
                        rasterHeight,
                        smallestOverviewSize
                    );

                if (vrtOverviewFactors.empty())
                {
                    preparedImagePath =
                        managedVrtPath.string();

                    std::cout
                        << "The managed VRT successfully isolated "
                        << "the invalid internal overviews, "
                        << "and the image is already small enough. "
                        << "No managed VRT overview is required."
                        << std::endl;

                    return true;
                }

                const std::string managedVrtOverviewPathString =
                    managedVrtOverviewPath.string();

                if (!buildExternalOverview(
                        managedVrtPath.string(),
                        vrtOverviewFactors))
                {
                    std::cerr
                        << "Failed to build the managed VRT overview."
                        << std::endl;

                    std::error_code removeError;

                    // 构建失败可能残留不完整的 .vrt.ovr。
                    if (std::filesystem::exists(
                            managedVrtOverviewPath))
                    {
                        std::filesystem::remove(
                            managedVrtOverviewPath,
                            removeError
                        );

                        if (removeError)
                        {
                            std::cerr
                                << "Failed to remove the incomplete "
                                << "managed VRT overview: "
                                << removeError.message()
                                << std::endl;
                        }
                    }

                    return false;
                }

                if (!checkOverviewImage(
                        managedVrtOverviewPathString,
                        rasterBandCount,
                        rasterWidth,
                        rasterHeight,
                        smallestOverviewSize))
                {
                    std::cerr
                        << "The managed VRT overview failed validation."
                        << std::endl;

                    std::error_code removeError;

                    if (std::filesystem::exists(
                            managedVrtOverviewPath))
                    {
                        std::filesystem::remove(
                            managedVrtOverviewPath,
                            removeError
                        );

                        if (removeError)
                        {
                            std::cerr
                                << "Failed to remove the invalid "
                                << "managed VRT overview: "
                                << removeError.message()
                                << std::endl;
                        }
                    }

                    return false;
                }

                std::cout
                    << "The managed VRT overview passed validation."
                    << std::endl;

                preparedImagePath =
                    managedVrtPath.string();

                std::cout
                    << "Prepared image path switched to: "
                    << preparedImagePath
                    << std::endl;

                return true;

            }
            else{
                std::cout
                    << "The image has its correct internaloverview."
                    << std::endl;
                
                return true;
            }
        }
    }
    
    return true;
}

int main(int argc, char** argv)
{
    osgEarth::initialize();
    GDALAllRegister();
    std::cout
        << "GDAL version: "
        << GDALVersionInfo("RELEASE_NAME")
        << std::endl;

    osg::ArgumentParser args(&argc, argv);
    osgViewer::Viewer viewer(args);

    const std::string imagePath ="D:/work/projects/HelloEarthWorkspace/testdata/ref7.tif";
    std::string preparedImagePath;
    if (!checkSingleImage(imagePath, preparedImagePath))
    {
        return -1;
    }


    // 为现代 OpenGL 环境启用顶点属性别名和矩阵 Uniform。
    viewer.setRealizeOperation(
        new osgEarth::GL3RealizeOperation()
    );
    // 读取tif影像前需要先声明GDALImageLayer类型，setURL告诉其影像路径，之后将其添加到MapNode中，最后设置场景数据为MapNode。
    auto imagery = new osgEarth::GDALImageLayer();
    imagery->setURL(
        preparedImagePath
    );

    auto mapNode = new osgEarth::MapNode();
    // addLayer步骤执行时，会让imagery去打开影像，读取tif影像的信息，之后可以通过imagery->getStatus().isError()来判断是否成功打开影像。
    mapNode->getMap()->addLayer(imagery);

    if (imagery->getStatus().isError()){
        std::cerr
            << "Failed to open image: "
            << imagery->getStatus().toString()
            << std::endl;

        return -1;
    }

    // getDataExtentsUnion用于获取layer中的四至范围并集
    const osgEarth::DataExtent& imageExtent = imagery->getDataExtentsUnion();
    // isValid()用于最低限度的判断extend中的四至范围是否有效，若无效则说明影像没有有效数据范围。
    if (!imageExtent.isValid()){
        std::cerr
            << "The image has no valid data extent."
            << std::endl;

        return -1;
    }
    else{
        std::cout
            << "Image extent:" << std::endl
            << "  X min: " << imageExtent.xMin() << std::endl
            << "  Y min: " << imageExtent.yMin() << std::endl
            << "  X max: " << imageExtent.xMax() << std::endl
            << "  Y max: " << imageExtent.yMax() << std::endl;
    }

    // getCentroid()用于获取extend的中心点坐标，返回值为GeoPoint类型，包含了空间参考系和三维坐标信息。
    const osgEarth::GeoPoint imageCenter=imageExtent.getCentroid();
    std::cout
        << "Image center:" << std::endl
        << "  X: " << imageCenter.x() << std::endl
        << "  Y: " << imageCenter.y() << std::endl
        << "  Z: " << imageCenter.z() << std::endl
        << "  SRS: " << imageCenter.getSRS()->getName()
        << std::endl;

    //因为osgearth的相机范围单位是米，所以相机的范围需要将影像的四至范围转换为米单位，osgEarth提供了Units类来进行单位转换，
    //使用imageExtent.width(osgEarth::Units::METERS)和imageExtent.height(osgEarth::Units::METERS)来获取影像的宽度和高度，单位为米。
    const double widthMeters = imageExtent.width(osgEarth::Units::METERS);
    const double heightMeters = imageExtent.height(osgEarth::Units::METERS);
    std::cout
        << "Image size on the ground:" << std::endl
        << "  Width: " << widthMeters << " meters" << std::endl
        << "  Height: " << heightMeters << " meters"
        << std::endl;
    
    // maxSpanMeters用于获取影像的最大跨度，方便后续设置相机的视野范围。
    const double maxSpanMeters = std::max(widthMeters, heightMeters);
    // cameraRangeMeters用于设置相机的范围，通常设置为最大跨度的两倍，以确保相机能够看到整个影像。
    const double cameraRangeMeters = maxSpanMeters * 2.0;
    std::cout
        << "Estimated camera range: "
        << cameraRangeMeters
        << " meters"
        << std::endl;

    // 默认构造出来的是一个空的 Viewpoint，还没有实际观察目标。
    osgEarth::Viewpoint initialViewpoint;
    // initialViewpoint.setFocalPoint(imageCenter);
    initialViewpoint.setFocalPoint(imageCenter);
    // heading 表示相机围绕观察中心的水平方向。这里设置为 0°。垂直俯视时，它主要影响影像在屏幕上的旋转方向。
    initialViewpoint.setHeading(
        osgEarth::Angle(0.0, osgEarth::Units::DEGREES)
    );
    // 设置俯仰角，pitch 表示相机围绕观察中心的垂直方向。这里设置为 -90°，表示相机垂直向下俯视影像。
    initialViewpoint.setPitch(
        osgEarth::Angle(-90.0, osgEarth::Units::DEGREES)
    );
    // 设置观察距离，range 表示相机与观察中心的距离。这里设置为影像最大跨度的两倍，以确保相机能够看到整个影像。
    initialViewpoint.setRange(
        osgEarth::Distance(cameraRangeMeters, osgEarth::Units::METERS)
    );
    // 输出初始视点信息，方便调试和验证设置是否正确。
    std::cout
        << "Initial viewpoint: "
        << initialViewpoint.toString()
        << std::endl;
    
    // 检查初始视点是否有效，如果无效则输出错误信息并退出程序。
    if (!initialViewpoint.isValid())
    {
        std::cerr
            << "Initial viewpoint is invalid."
            << std::endl;

        return -1;
    }
    
    viewer.setSceneData(mapNode);

    // 设置相机操纵器为 EarthManipulator，并将初始视点应用到相机上。
    // 先给viewer设置一个EarthManipulator，然后使用setViewpoint方法将初始视点应用到相机上，确保程序启动时相机能够正确地观察到影像。
    auto manipulator = new osgEarth::EarthManipulator(args);
    viewer.setCameraManipulator(manipulator);
    manipulator->setHomeViewpoint(
        initialViewpoint,
        0.0
    );
    // manipulator->setViewpoint(
    //     initialViewpoint,
    //     0.0
    // );

    return viewer.run();
}