cmake_minimum_required(VERSION 3.21)
project(PHSRadio VERSION 0.2.1 LANGUAGES CXX)

option(PHSRADIO_KUGOU_ONLY "Publish a release with only the Kugou platform enabled" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets Multimedia Network)
find_package(ICU REQUIRED COMPONENTS i18n uc data)

add_library(MusicSearch STATIC music_search.h music_search.cpp)
target_link_libraries(MusicSearch PUBLIC Qt6::Core PRIVATE ICU::i18n ICU::uc ICU::data)

add_library(PlaybackQueue STATIC playback_queue.h playback_queue.cpp)
target_link_libraries(PlaybackQueue PUBLIC Qt6::Core)

add_library(MusicLyrics STATIC lyric_domain.h lyrics.h lyrics.cpp)
target_link_libraries(MusicLyrics PUBLIC Qt6::Core)

add_library(AeroWidgets STATIC
    aero_widgets.h aero_widgets.cpp aero_surface.h aero_surface.cpp
    aero_animation.h aero_animation.cpp meteor_effect.h meteor_effect.cpp
    liquid_backdrop.h liquid_backdrop.cpp
    playlist_drawer.h playlist_drawer.cpp playback_dock.h playback_dock.cpp
    song_glass_delegate.h song_glass_delegate.cpp
    catalog_search_panel.h catalog_search_panel.cpp
    home_page.h home_page.cpp song_page.h song_page.cpp
    background_theme.h background_theme.cpp theme_panel.h theme_panel.cpp
    wallpaper_engine_capture.h wallpaper_engine_capture.cpp
    glass_title_bar.h glass_title_bar.cpp
    resources/platforms.qrc resources/app.qrc)
target_link_libraries(AeroWidgets PUBLIC Qt6::Widgets Qt6::Multimedia)
if(WIN32)
    target_link_libraries(AeroWidgets PRIVATE user32)
endif()

qt_add_executable(PHSRadio WIN32
    main.cpp
    music_domain.h
    music_provider.h
    mock_music_provider.h
    mock_music_provider.cpp
    platform_login_page.h
    platform_login_page.cpp
    kugou_api_client.h
    kugou_api_client.cpp
    playlist_snapshot_provider.h
    playlist_snapshot_provider.cpp
    netease_api_client.h netease_api_client.cpp
    netease_music_provider.h netease_music_provider.cpp
)

target_compile_definitions(PHSRadio PRIVATE
    PHSRADIO_KUGOU_SERVICE_DIR="${CMAKE_SOURCE_DIR}/services/kugou"
    PHSRADIO_NETEASE_SERVICE_DIR="${CMAKE_SOURCE_DIR}/services/netease"
    PHSRADIO_VERSION="${PROJECT_VERSION}"
)
if(PHSRADIO_KUGOU_ONLY)
    target_compile_definitions(PHSRadio PRIVATE PHSRADIO_KUGOU_ONLY=1)
endif()
set(PHSRADIO_APP_OUTPUT_DIR "" CACHE PATH "Optional preview executable directory")
if(PHSRADIO_APP_OUTPUT_DIR)
    set_target_properties(PHSRadio PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PHSRADIO_APP_OUTPUT_DIR}")
endif()

if(WIN32)
    enable_language(RC)
    configure_file(resources/windows_version.rc.in windows_version.rc @ONLY)
    configure_file(resources/app.ico app.ico COPYONLY)
    if(MINGW)
        # windres invokes an ANSI preprocessor on Windows. Relative paths keep
        # resource builds working when the project directory has Chinese characters.
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/windows_version.obj"
            COMMAND "${CMAKE_RC_COMPILER}" -O coff -i windows_version.rc -o windows_version.obj
            DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/windows_version.rc" "${CMAKE_CURRENT_BINARY_DIR}/app.ico"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            VERBATIM)
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/windows_version.obj"
            PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_sources(PHSRadio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows_version.obj")
    else()
        target_sources(PHSRadio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows_version.rc")
    endif()
endif()

target_link_libraries(PHSRadio PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network MusicSearch MusicLyrics PlaybackQueue AeroWidgets crypt32)

include(CTest)
if(BUILD_TESTING)
    add_executable(DeviceRegistrationTest tests/device_registration_test.cpp)
    target_link_libraries(DeviceRegistrationTest PRIVATE Qt6::Gui Qt6::Network MusicLyrics crypt32)
    add_test(NAME device_registration COMMAND DeviceRegistrationTest)
    set_tests_properties(device_registration PROPERTIES TIMEOUT 15)
    add_executable(LibraryLoadingTest tests/library_loading_test.cpp)
    target_link_libraries(LibraryLoadingTest PRIVATE Qt6::Gui Qt6::Network MusicSearch MusicLyrics crypt32)
    add_test(NAME library_loading COMMAND LibraryLoadingTest)
    set_tests_properties(library_loading PROPERTIES TIMEOUT 15)
    add_executable(LibraryViewTest tests/library_view_test.cpp
        mock_music_provider.cpp platform_login_page.cpp kugou_api_client.cpp playlist_snapshot_provider.cpp
        netease_api_client.cpp netease_music_provider.cpp)
    target_link_libraries(LibraryViewTest PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network MusicSearch MusicLyrics PlaybackQueue AeroWidgets crypt32)
    add_test(NAME library_view COMMAND LibraryViewTest)
    set_tests_properties(library_view PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(MusicSearchTest tests/music_search_test.cpp)
    target_link_libraries(MusicSearchTest PRIVATE MusicSearch)
    add_test(NAME music_search COMMAND MusicSearchTest)
    set_tests_properties(music_search PROPERTIES TIMEOUT 20)
    add_executable(PlaylistDrawerTest tests/playlist_drawer_test.cpp)
    target_link_libraries(PlaylistDrawerTest PRIVATE AeroWidgets)
    add_test(NAME playlist_drawer COMMAND PlaylistDrawerTest)
    set_tests_properties(playlist_drawer PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(SearchLookupTest tests/search_lookup_test.cpp)
    target_link_libraries(SearchLookupTest PRIVATE Qt6::Gui Qt6::Network MusicSearch MusicLyrics crypt32)
    add_test(NAME search_lookup COMMAND SearchLookupTest)
    set_tests_properties(search_lookup PROPERTIES TIMEOUT 15)
    add_executable(CatalogSearchIntegrationTest tests/catalog_search_integration_test.cpp
        mock_music_provider.cpp platform_login_page.cpp kugou_api_client.cpp playlist_snapshot_provider.cpp
        netease_api_client.cpp netease_music_provider.cpp)
    target_link_libraries(CatalogSearchIntegrationTest PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network MusicSearch MusicLyrics PlaybackQueue AeroWidgets crypt32)
    add_test(NAME catalog_search_integration COMMAND CatalogSearchIntegrationTest)
    set_tests_properties(catalog_search_integration PROPERTIES TIMEOUT 25 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(PlaybackQueueTest tests/playback_queue_test.cpp)
    target_link_libraries(PlaybackQueueTest PRIVATE PlaybackQueue)
    add_test(NAME playback_queue COMMAND PlaybackQueueTest)
    set_tests_properties(playback_queue PROPERTIES TIMEOUT 15)
    add_executable(PlaybackDockTest tests/playback_dock_test.cpp)
    target_link_libraries(PlaybackDockTest PRIVATE AeroWidgets)
    add_test(NAME playback_dock COMMAND PlaybackDockTest)
    set_tests_properties(playback_dock PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(SongGlassTest tests/song_glass_test.cpp)
    target_link_libraries(SongGlassTest PRIVATE AeroWidgets)
    add_test(NAME song_glass COMMAND SongGlassTest)
    set_tests_properties(song_glass PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(PlaybackIntegrationTest tests/playback_integration_test.cpp
        mock_music_provider.cpp platform_login_page.cpp kugou_api_client.cpp playlist_snapshot_provider.cpp
        netease_api_client.cpp netease_music_provider.cpp)
    target_link_libraries(PlaybackIntegrationTest PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network MusicSearch MusicLyrics PlaybackQueue AeroWidgets crypt32)
    add_test(NAME playback_integration COMMAND PlaybackIntegrationTest)
    set_tests_properties(playback_integration PROPERTIES TIMEOUT 40 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(LiquidBackdropTest tests/liquid_backdrop_test.cpp)
    target_link_libraries(LiquidBackdropTest PRIVATE AeroWidgets)
    add_test(NAME liquid_backdrop COMMAND LiquidBackdropTest)
    set_tests_properties(liquid_backdrop PROPERTIES TIMEOUT 20 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(AeroAnimationTest tests/aero_animation_test.cpp)
    target_link_libraries(AeroAnimationTest PRIVATE AeroWidgets)
    add_test(NAME aero_animation COMMAND AeroAnimationTest)
    set_tests_properties(aero_animation PROPERTIES TIMEOUT 10 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(MeteorEffectTest tests/meteor_effect_test.cpp)
    target_link_libraries(MeteorEffectTest PRIVATE AeroWidgets)
    add_test(NAME meteor_effect COMMAND MeteorEffectTest)
    set_tests_properties(meteor_effect PROPERTIES TIMEOUT 10 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(BackgroundThemeTest tests/background_theme_test.cpp background_theme.cpp)
    target_link_libraries(BackgroundThemeTest PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME background_theme COMMAND BackgroundThemeTest)
    set_tests_properties(background_theme PROPERTIES TIMEOUT 15)
    add_executable(BackgroundSurfaceTest tests/background_surface_test.cpp)
    target_link_libraries(BackgroundSurfaceTest PRIVATE AeroWidgets)
    add_test(NAME background_surface COMMAND BackgroundSurfaceTest)
    set_tests_properties(background_surface PROPERTIES TIMEOUT 20 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(HomeSongUiTest tests/home_song_ui_test.cpp)
    target_link_libraries(HomeSongUiTest PRIVATE AeroWidgets)
    add_test(NAME home_song_ui COMMAND HomeSongUiTest)
    set_tests_properties(home_song_ui PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(HomeIntegrationTest tests/home_integration_test.cpp
        mock_music_provider.cpp platform_login_page.cpp kugou_api_client.cpp playlist_snapshot_provider.cpp
        netease_api_client.cpp netease_music_provider.cpp)
    target_link_libraries(HomeIntegrationTest PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network MusicSearch MusicLyrics PlaybackQueue AeroWidgets crypt32)
    add_test(NAME home_integration COMMAND HomeIntegrationTest)
    set_tests_properties(home_integration PROPERTIES TIMEOUT 25 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(ReleasePlatformGateTest tests/release_platform_gate_test.cpp
        mock_music_provider.cpp platform_login_page.cpp kugou_api_client.cpp playlist_snapshot_provider.cpp
        netease_api_client.cpp netease_music_provider.cpp)
    target_compile_definitions(ReleasePlatformGateTest PRIVATE PHSRADIO_KUGOU_ONLY=1)
    target_link_libraries(ReleasePlatformGateTest PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::Network MusicSearch MusicLyrics PlaybackQueue AeroWidgets crypt32)
    add_test(NAME release_platform_gate COMMAND ReleasePlatformGateTest)
    set_tests_properties(release_platform_gate PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(LyricsTest tests/lyrics_test.cpp)
    target_link_libraries(LyricsTest PRIVATE MusicLyrics)
    add_test(NAME lyrics COMMAND LyricsTest)
    set_tests_properties(lyrics PROPERTIES TIMEOUT 10)
    add_executable(RecommendationApiTest tests/recommendation_api_test.cpp)
    target_link_libraries(RecommendationApiTest PRIVATE Qt6::Gui Qt6::Network MusicSearch MusicLyrics crypt32)
    add_test(NAME recommendation_api COMMAND RecommendationApiTest)
    set_tests_properties(recommendation_api PROPERTIES TIMEOUT 20)
    add_executable(GlassTitleBarTest tests/glass_title_bar_test.cpp)
    target_link_libraries(GlassTitleBarTest PRIVATE AeroWidgets)
    add_test(NAME glass_title_bar COMMAND GlassTitleBarTest)
    set_tests_properties(glass_title_bar PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
