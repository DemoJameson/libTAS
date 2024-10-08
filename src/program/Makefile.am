bin_PROGRAMS = libTAS

libTAS_QTSOURCES = \
    GameEvents.h \
    GameLoop.h \
    movie/MovieFileChangeLog.h \
    movie/MovieFileInputs.h \
    lua/Print.h \
    ramsearch/MemScanner.h \
    ui/AnnotationsWindow.h \
    ui/ComboBoxItemDelegate.h \
    ui/ControllerAxisWidget.h \
    ui/ControllerTabWindow.h \
    ui/ControllerWidget.h \
    ui/EncodeWindow.h \
    ui/ExecutableWindow.h \
    ui/GameInfoWindow.h \
    ui/InputChangeLogModel.h \
    ui/InputChangeLogWindow.h \
    ui/InputEditorModel.h \
    ui/InputEditorView.h \
    ui/InputEditorWindow.h \
    ui/InputEventModel.h \
    ui/InputEventWindow.h \
    ui/InputWindow.h \
    ui/KeyPressedDialog.h \
    ui/LuaConsoleWindow.h \
    ui/LuaConsoleModel.h \
    ui/MainWindow.h \
    ui/MarkerModel.h \
    ui/MarkerView.h \
    ui/MovieSettingsWindow.h \
    ui/PointerScanModel.h \
    ui/PointerScanWindow.h \
    ui/RamSearchModel.h \
    ui/RamSearchWindow.h \
    ui/RamWatchEditWindow.h \
    ui/RamWatchModel.h \
    ui/RamWatchView.h \
    ui/RamWatchWindow.h \
    ui/TimeTraceModel.h \
    ui/TimeTraceWindow.h \
    ui/settings/RuntimePane.h \
    ui/settings/AudioPane.h \
    ui/settings/InputPane.h \
    ui/settings/MoviePane.h \
    ui/settings/VideoPane.h \
    ui/settings/DebugPane.h \
    ui/settings/GameSpecificPane.h \
    ui/settings/SettingsWindow.h \
    ui/settings/tooltip/BalloonTip.h

libTAS_MOCSOURCES = $(libTAS_QTSOURCES:.h=_moc.cpp)

libTAS_SOURCES = \
    AutoDetect.cpp \
    AutoSave.cpp \
    Config.cpp \
    GameEvents.cpp \
    GameEventsXcb.cpp \
    GameLoop.cpp \
    GameThread.cpp \
    KeyMapping.cpp \
    KeyMappingXcb.cpp \
    main.cpp \
    SaveState.cpp \
    SaveStateList.cpp \
    utils.cpp \
    lua/Callbacks.cpp \
    lua/Gui.cpp \
    lua/LuaFunctionList.cpp \
    lua/NamedLuaFunction.cpp \
    lua/Input.cpp \
    lua/Main.cpp \
    lua/Memory.cpp \
    lua/Movie.cpp \
    lua/Print.cpp \
    lua/Runtime.cpp \
    movie/InputSerialization.cpp \
    movie/MovieActionEditFrames.cpp \
    movie/MovieActionInsertFrames.cpp \
    movie/MovieActionPaint.cpp \
    movie/MovieActionRemoveFrames.cpp \
    movie/MovieFile.cpp \
    movie/MovieFileAnnotations.cpp \
    movie/MovieFileChangeLog.cpp \
    movie/MovieFileEditor.cpp \
    movie/MovieFileHeader.cpp \
    movie/MovieFileInputs.cpp \
    ui/AnnotationsWindow.cpp \
    ui/ComboBoxItemDelegate.cpp \
    ui/ControllerAxisWidget.cpp \
    ui/ControllerTabWindow.cpp \
    ui/ControllerWidget.cpp \
    ui/EncodeWindow.cpp \
    ui/ErrorChecking.cpp \
    ui/ExecutableWindow.cpp \
    ui/GameInfoWindow.cpp \
    ui/InputChangeLogModel.cpp \
    ui/InputChangeLogWindow.cpp \
    ui/InputEditorModel.cpp \
    ui/InputEditorView.cpp \
    ui/InputEditorWindow.cpp \
    ui/InputEventModel.cpp \
    ui/InputEventWindow.cpp \
    ui/InputWindow.cpp \
    ui/KeyPressedDialog.cpp \
    ui/LuaConsoleWindow.cpp \
    ui/LuaConsoleModel.cpp \
    ui/MainWindow.cpp \
    ui/MarkerModel.cpp \
    ui/MarkerView.cpp \
    ui/MovieSettingsWindow.cpp \
    ui/PointerScanModel.cpp \
    ui/PointerScanWindow.cpp \
    ui/RamSearchModel.cpp \
    ui/RamSearchWindow.cpp \
    ui/RamWatchEditWindow.cpp \
    ui/RamWatchModel.cpp \
    ui/RamWatchView.cpp \
    ui/RamWatchWindow.cpp \
    ui/TimeTraceModel.cpp \
    ui/TimeTraceWindow.cpp \
    ui/qtutils.cpp \
    ui/settings/RuntimePane.cpp \
    ui/settings/AudioPane.cpp \
    ui/settings/InputPane.cpp \
    ui/settings/MoviePane.cpp \
    ui/settings/VideoPane.cpp \
    ui/settings/DebugPane.cpp \
    ui/settings/GameSpecificPane.cpp \
    ui/settings/SettingsWindow.cpp \
    ui/settings/WrapInScrollArea.cpp \
    ui/settings/tooltip/BalloonTip.cpp \
    ui/settings/tooltip/ToolTipCheckBox.cpp \
    ui/settings/tooltip/ToolTipComboBox.cpp \
    ui/settings/tooltip/ToolTipGroupBox.cpp \
    ui/settings/tooltip/ToolTipRadioButton.cpp \
    ui/settings/tooltip/ToolTipSlider.cpp \
    ui/settings/tooltip/ToolTipSpinBox.cpp \
    ramsearch/BaseAddresses.cpp \
    ramsearch/CompareOperations.cpp \
    ramsearch/RamWatchDetailed.cpp \
    ramsearch/MemAccess.cpp \
    ramsearch/MemLayout.cpp \
    ramsearch/MemScanner.cpp \
    ramsearch/MemScannerThread.cpp \
    ramsearch/MemSection.cpp \
    ramsearch/MemValue.cpp \
    ../shared/inputs/AllInputs.cpp \
    ../shared/inputs/ControllerInputs.cpp \
    ../shared/inputs/MiscInputs.cpp \
    ../shared/inputs/MouseInputs.cpp \
    ../shared/inputs/SingleInput.cpp \
    ../shared/sockethelpers.cpp \
    $(libTAS_MOCSOURCES)

libTAS_CXXFLAGS = $(QT5_CFLAGS) $(LIBLUA_CFLAGS) -fno-stack-protector -Wno-float-equal -fPIC
libTAS_LDADD = $(QT5_LIBS) $(LIBLUA_LIBS) $(PROGRAM_LIBS)

.h_moc.cpp:
	@MOC@ -o $@ $(QT5_CFLAGS) $<

SUFFIXES = .h _moc.cpp

CLEANFILES = $(libTAS_MOCSOURCES)
