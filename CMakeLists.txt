cmake_minimum_required(VERSION 3.21)
project(VirtualLab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network WebSockets SerialPort SerialBus Sql Svg)
qt_standard_project_setup(REQUIRES 6.5)

add_library(scada_core STATIC
    src/project.cpp src/project.h
    src/modbus.cpp src/modbus.h src/datasource.h
    src/engine.cpp src/engine.h
    src/drivers.cpp src/drivers.h
    src/runtime.cpp src/runtime.h
    src/demo.cpp src/demo.h
    src/studio.cpp src/studio.h
)
target_include_directories(scada_core PUBLIC src)
target_link_libraries(scada_core PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets Qt6::SerialPort Qt6::SerialBus Qt6::Sql)
if(MSVC)
    target_compile_options(scada_core PUBLIC /utf-8)
endif()

qt_add_executable(scada WIN32 src/main.cpp)
set_target_properties(scada PROPERTIES OUTPUT_NAME "Virtual-Lab")
if(WIN32)
    enable_language(RC)
    target_sources(scada PRIVATE assets/virtual-lab.rc)
    target_include_directories(scada PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    set_source_files_properties(assets/virtual-lab.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/virtual-lab.ico")
endif()
target_link_libraries(scada PRIVATE scada_core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
qt_add_resources(scada "studio_qml"
    PREFIX "/"
    FILES assets/virtual-lab.svg assets/virtual-lab.ico
          qml/Main.qml qml/Dashboard.qml qml/ScreenItem.qml qml/Trend.qml
          qml/SourceDialog.qml qml/TagDialog.qml qml/Inspector.qml qml/Events.qml
          qml/Field.qml qml/LabelValue.qml qml/History.qml qml/ArchivePlot.qml qml/ActionButton.qml qml/InstrumentFace.qml
          qml/DashboardTrend.qml qml/TrendDialog.qml qml/AppearanceDialog.qml qml/ColorField.qml
          qml/ScreenProperties.qml qml/ControlFace.qml qml/ProcessSymbol.qml qml/SelectorDialog.qml
          qml/WidgetActionsDialog.qml qml/ActionEventButton.qml
          qml/FaceplateContent.qml qml/FaceplatePortsDialog.qml qml/FaceplateBindingsDialog.qml
          qml/FaceplatePropertyInterfaceDialog.qml qml/FaceplateInstancePropertiesDialog.qml
          qml/FaceplatePropertyBindingDialog.qml qml/FaceplateFieldMapping.qml
          qml/WidgetLayers.qml qml/WidgetPreview.qml qml/LayerAnimationDialog.qml
)
qt_add_resources(scada_core "project_assets" PREFIX "/"
    FILES engine-tags.json assets/demo-loop.png assets/pump-idle.svg assets/pump-running.svg assets/pump-impeller.svg)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_sources(scada PRIVATE tests/ui_flow.cpp tests/ui_audit.cpp)
    target_compile_definitions(scada PRIVATE SCADA_UI_TEST)
    target_link_libraries(scada PRIVATE Qt6::Test)
    add_executable(scada_tests tests/core_test.cpp)
    target_link_libraries(scada_tests PRIVATE scada_core Qt6::Test)
    add_test(NAME scada_core COMMAND scada_tests -o -,txt)
    add_test(NAME scada_ui COMMAND scada --ui-test)
    add_test(NAME scada_ui_audit COMMAND scada --ui-audit)
    set(ui_test_environment "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1")
    if(WIN32 AND NOT DEFINED ENV{QT_QPA_FONTDIR} AND IS_DIRECTORY "$ENV{WINDIR}/Fonts")
        list(APPEND ui_test_environment "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    endif()
    set_tests_properties(scada_ui scada_ui_audit PROPERTIES
        ENVIRONMENT "${ui_test_environment}"
        RESOURCE_LOCK scada_demo_port TIMEOUT 90)
    set_tests_properties(scada_core PROPERTIES TIMEOUT 45)
endif()
