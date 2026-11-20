# onnxruntime overlay port: repackages the official Microsoft.ML.OnnxRuntime
# NuGet native package instead of building from source.
#
# The registry port builds from a GitHub source tarball
# (github.com/microsoft/onnxruntime/archive/v<ver>.tar.gz). That repository
# disappeared in November 2026 (404 on every archive request), so a cold
# vcpkg cache cannot install the registry port. This overlay supplies the
# same headers, import libraries and runtime DLLs, version-matched to
# 1.32.0, straight from the official NuGet package.
#
# Layout mirrors what the registry port installs so consumers need no
# changes: headers under include/onnxruntime, import libs under lib,
# runtime DLLs under bin.

set(ORT_VERSION "1.32.0")
if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(ORT_RID "win-arm64")
else()
    set(ORT_RID "win-x64")
endif()

vcpkg_download_distfile(NUPKG
    URLS "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${ORT_VERSION}/microsoft.ml.onnxruntime.${ORT_VERSION}.nupkg"
    FILENAME "microsoft.ml.onnxruntime.${ORT_VERSION}.zip"
    SHA512 7e3d8ff6ce68dace4cbf0c1d0e2085e1faf05d425e77580b875c1bd22802527a276ebbdfa07326005b5a8fd6ad8186c17e5710b44dc543acc6bffb3be45404b1
)

vcpkg_extract_source_archive(PACKAGE_DIR
    ARCHIVE "${NUPKG}"
    NO_REMOVE_ONE_LEVEL
)

# Headers (nupkg: build/native/include/*.h)
file(COPY "${PACKAGE_DIR}/build/native/include/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/onnxruntime")

# Import libraries
file(COPY
    "${PACKAGE_DIR}/runtimes/${ORT_RID}/native/onnxruntime.lib"
    "${PACKAGE_DIR}/runtimes/${ORT_RID}/native/onnxruntime_providers_shared.lib"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

# Runtime DLLs
file(COPY
    "${PACKAGE_DIR}/runtimes/${ORT_RID}/native/onnxruntime.dll"
    "${PACKAGE_DIR}/runtimes/${ORT_RID}/native/onnxruntime_providers_shared.dll"
    DESTINATION "${CURRENT_PACKAGES_DIR}/bin")

# License
file(COPY "${PACKAGE_DIR}/LICENSE"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/onnxruntime")

file(WRITE "${CURRENT_PACKAGES_DIR}/share/onnxruntime/copyright" "")
