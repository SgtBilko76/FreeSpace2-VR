# Android (Meta Quest): the OpenXR loader cannot be built from lib/openxr (no jnipp),
# so link against the official Khronos loader AAR. The APK packages the same AAR
# through Gradle (org.khronos.openxr:openxr_loader_for_android), so the library
# found at runtime matches the one linked here.
set(OPENXR_ANDROID_LOADER_VERSION "1.1.63")
set(_aar_dir "${CMAKE_CURRENT_BINARY_DIR}/openxr_loader_for_android-${OPENXR_ANDROID_LOADER_VERSION}")
set(_aar_file "${_aar_dir}.aar")
set(_loader_so "${_aar_dir}/prefab/modules/openxr_loader/libs/android.${ANDROID_ABI}/libopenxr_loader.so")

if(NOT EXISTS "${_loader_so}")
	set(_url "https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/${OPENXR_ANDROID_LOADER_VERSION}/openxr_loader_for_android-${OPENXR_ANDROID_LOADER_VERSION}.aar")
	message(STATUS "Downloading OpenXR Android loader from ${_url}")
	file(DOWNLOAD "${_url}" "${_aar_file}" STATUS _status TLS_VERIFY ON)
	list(GET _status 0 _code)
	if(NOT _code EQUAL 0)
		message(FATAL_ERROR "Failed to download the OpenXR Android loader: ${_status}")
	endif()
	file(MAKE_DIRECTORY "${_aar_dir}")
	file(ARCHIVE_EXTRACT INPUT "${_aar_file}" DESTINATION "${_aar_dir}")
endif()

if(NOT EXISTS "${_loader_so}")
	message(FATAL_ERROR "OpenXR Android loader not found at ${_loader_so}")
endif()

add_library(OpenXR::openxr_loader SHARED IMPORTED GLOBAL)
set_target_properties(OpenXR::openxr_loader PROPERTIES
	IMPORTED_LOCATION "${_loader_so}"
	IMPORTED_NO_SONAME TRUE
	# Use the engine's pinned OpenXR headers, not the AAR's newer ones
	INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/openxr/include")

add_library(OpenXR::Headers INTERFACE IMPORTED GLOBAL)
set_target_properties(OpenXR::Headers PROPERTIES
	INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/openxr/include")
