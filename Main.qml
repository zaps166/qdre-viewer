pragma ComponentBehavior: Bound

// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import "Defines.js" as Defines

ApplicationWindow {
    id: appWindow

    readonly property string directoryPositionTitle: Singleton.directoryCount > 0
                                                     ? (Singleton.directoryPosition >= 0 ? Singleton.directoryPosition + 1 : "?") + " / " + Singleton.directoryCount
                                                     : ""
    readonly property string currentFileName: Singleton.getFileName(view.currentPath)

    property bool wasMaximizedBeforeFullScreen

    property bool displayColorManagement: true
    property bool useGammaForSRgb
    property int viewerMode: Defines.VIEWER_MODE_NONE

    minimumWidth: Math.max(splitView.minimumWidth, toolBar.implicitWidth) + 10
    minimumHeight: toolBar.implicitHeight + 200

    visible: true

    title: {
        if (view.hasCurrentPath) {
            const fileTitle = currentFileName + (directoryPositionTitle !== "" ? " - " + directoryPositionTitle : "")
            if (view.hasReadyPath && Number.isFinite(view.imageZoom) && view.imageZoom > 0.0) {
                return fileTitle + " - " + Math.round(view.imageZoom * 100.0) + "%"
            }
            return fileTitle
        }
        return ""
    }

    function setupViewerMode() {
        viewerMode = (displayColorManagement && Singleton.isWayland)
                ? Defines.VIEWER_MODE_CONTAINER
                : Defines.VIEWER_MODE_DIRECT
    }

    Component.onCompleted: {
        Singleton.setMainWindow(appWindow)
        setupViewerMode()
    }

    onClosing: {
        if (settingsLoader.active) {
            (settingsLoader.item as MySettings).storeData();
        }
    }

    onColorChanged: {
        Singleton.updateWindowColor()
    }

    onDisplayColorManagementChanged: {
        Singleton.setDisplayColorManagementEnabled(displayColorManagement)
        if (viewerMode !== Defines.VIEWER_MODE_NONE) {
            setupViewerMode()
        }
    }
    onUseGammaForSRgbChanged: {
        Singleton.setUseGammaForSRgb(useGammaForSRgb)
    }

    Connections {
        target: Singleton

        function onLoadFile(filePath: string) {
            if (view.visible) {
                view.setPath(filePath)
            }
        }
        function onReload() {
            if (view.visible) {
                view.reload()
            }
        }

        function onMetadataChanged(hflip: bool, vflip: bool, rotation: real, text: string) {
            view.hflipMetadata = hflip
            view.vflipMetadata = vflip
            view.rotationMetadata = rotation
            metadataText.text = text
        }
        function onAnimationFrameDelayChanged(nextAnimationFrameDelay: int) {
            view.animationSequence(nextAnimationFrameDelay)
        }
    }

    component MySettings : Settings {
        property int x: (1 << 31)
        property int y: (1 << 31)
        property int width: 960
        property int height: 540

        property bool maximized

        property bool metadataPanelWindowed: true
        property bool metadataPanelFullScreen

        property bool smoothUpscale: true
        property bool displayColorManagement: true
        property bool useGammaForSRgb

        property string rhiBackend

        Component.onCompleted: {
            if (!Singleton.isWayland) {
                if (x != (1 << 31)) {
                    appWindow.x = x
                }
                if (y != (1 << 31)) {
                    appWindow.y = y
                }
            }
            appWindow.width = width
            appWindow.height = height

            if (maximized) {
                appWindow.showMaximized()
            }

            metadataScroll.showWindowed = metadataPanelWindowed
            metadataScroll.showFullScreen = metadataPanelFullScreen

            view.smoothUpscale = smoothUpscale
            appWindow.displayColorManagement = displayColorManagement
            appWindow.useGammaForSRgb = useGammaForSRgb
        }

        function storeData() {
            if (appWindow.visibility === Window.Windowed) {
                if (!Singleton.isWayland) {
                    x = appWindow.x
                    y = appWindow.y
                }
                width = appWindow.width
                height = appWindow.height
            }

            if (appWindow.visibility === Window.Maximized || appWindow.visibility === Window.Windowed) {
                maximized = appWindow.visibility === Window.Maximized
            }

            metadataPanelWindowed = metadataScroll.showWindowed
            metadataPanelFullScreen = metadataScroll.showFullScreen

            smoothUpscale = view.smoothUpscale
            displayColorManagement = appWindow.displayColorManagement
            useGammaForSRgb = appWindow.useGammaForSRgb
        }
    }
    Loader {
        id: settingsLoader

        active: Singleton.configPath.toString() !== ""

        sourceComponent: MySettings {
            location: Singleton.configPath
        }
    }

    FileDialog {
        id: fileDialog

        title: qsTr("Choose image file")

        currentFolder: view.hasCurrentPath
                       ? Singleton.getDirectoryPathUrl(view.currentPath)
                       : StandardPaths.standardLocations(StandardPaths.PicturesLocation)[0]

        onAccepted: {
            let selected = selectedFile.toString()
            if (selected.startsWith("file://")) {
                Singleton.openFile(Singleton.urlToLocalFile(selected))
            }
        }
    }

    Loader {
        id: trashDialogLoader

        property string toDelete

        active: toDelete !== ""

        sourceComponent: MessageDialog {
            Component.onCompleted: {
                open()
            }

            text: qsTr("Do you want to move %1 to trash?").arg("<b>" + trashDialogLoader.toDelete + "</b>")

            buttons: MessageDialog.Yes | MessageDialog.No

            onAccepted: {
                if (Singleton.moveToTrash(trashDialogLoader.toDelete)) {
                    if (actionNext.enabled) {
                        actionNext.trigger()
                    } else if (actionPrev.enabled) {
                        actionPrev.trigger()
                    }
                    Singleton.removeFromFileList(trashDialogLoader.toDelete)
                }
                trashDialogLoader.toDelete = ""
            }
            onRejected: {
                trashDialogLoader.toDelete = ""
            }
        }
    }

    component MyAction: Action {
        id: myActionComponent

        property var sequence
        property list<var> sequences
        property bool autoRepeat: true

        shortcut: Shortcut {
            sequence: myActionComponent.sequence
            sequences: myActionComponent.sequences
            autoRepeat: myActionComponent.autoRepeat
            onActivated: myActionComponent.trigger()
        }
    }

    MyAction {
        id: actionOpen
        text: qsTr("Open")
        icon.name: "document-open"
        sequence: "Ctrl+O"
        autoRepeat: false
        onTriggered: {
            fileDialog.open()
        }
    }
    MyAction {
        id: actionFirst
        enabled: Singleton.directoryCount > 0 && Singleton.directoryPosition !== 0
        text: qsTr("First")
        icon.name: "go-first"
        sequence: "Home"
        autoRepeat: false
        onTriggered: {
            Singleton.navigateBegin()
        }
    }
    MyAction {
        id: actionLast
        enabled: Singleton.directoryCount > 0 && Singleton.directoryPosition !== Singleton.directoryCount - 1
        text: qsTr("Last")
        icon.name: "go-last"
        sequence: "End"
        autoRepeat: false
        onTriggered: {
            Singleton.navigateEnd()
        }
    }
    MyAction {
        id: actionPrev
        enabled: Singleton.directoryPosition > 0
        text: qsTr("Previous")
        icon.name: "go-previous"
        sequence: "Left"
        onTriggered: {
            Singleton.navigatePrev()
        }
    }
    MyAction {
        id: actionNext
        enabled: Singleton.directoryPosition >= 0 && Singleton.directoryPosition < Singleton.directoryCount - 1
        text: qsTr("Next")
        icon.name: "go-next"
        sequence: "Right"
        onTriggered: {
            Singleton.navigateNext()
        }
    }
    MyAction {
        id: actionZoomIn
        enabled: view.hasReadyPath
        text: qsTr("Zoom in")
        icon.name: "zoom-in"
        sequence: "+"
        onTriggered: {
            view.zoomIn()
        }
    }
    MyAction {
        id: actionZoomOut
        enabled: view.hasReadyPath
        text: qsTr("Zoom out")
        icon.name: "zoom-out"
        sequence: "-"
        onTriggered: {
            view.zoomOut()
        }
    }
    MyAction {
        id: actionOrigSize
        enabled: view.hasReadyPath
        text: qsTr("Original image size")
        icon.name: "zoom-original"
        sequence: "1"
        autoRepeat: false
        onTriggered: {
            view.originalSizeView()
        }
    }
    MyAction {
        id: actionFitView
        enabled: view.hasReadyPath
        text: qsTr("Fit image to window")
        icon.name: "zoom-fit-best"
        sequence: "F"
        autoRepeat: false
        onTriggered: {
            view.fitToWindow()
        }
    }
    MyAction {
        id: actionRotateCCW
        enabled: view.hasReadyPath
        text: qsTr("Rotate counter-clockwise")
        icon.name: "object-rotate-left"
        sequence: "Ctrl+L"
        onTriggered: {
            view.rotate(-1)
        }
    }
    MyAction {
        id: actionRotateCW
        enabled: view.hasReadyPath
        text: qsTr("Rotate clockwise")
        icon.name: "object-rotate-right"
        sequence: "Ctrl+R"
        onTriggered: {
            view.rotate(1)
        }
    }
    MyAction {
        id: actionHorizontalFlip
        enabled: view.hasReadyPath
        text: qsTr("Horizontal flip")
        checkable: true
        checked: view.hflipUser
        icon.name: "object-flip-horizontal"
        sequence: "Ctrl+H"
        onTriggered: {
            view.hflipUser = checked
        }
    }
    MyAction {
        id: actionVerticalFlip
        enabled: view.hasReadyPath
        text: qsTr("Vertical flip")
        checkable: true
        checked: view.vflipUser
        icon.name: "object-flip-vertical"
        sequence: "Ctrl+V"
        onTriggered: {
            view.vflipUser = checked
        }
    }
    MyAction {
        id: actionReload
        enabled: view.hasCurrentPath
        text: qsTr("Reload")
        icon.name: "view-refresh"
        sequence: "F5"
        autoRepeat: false
        onTriggered: {
            view.reload()
        }
    }
    MyAction {
        id: actionTrash
        enabled: view.hasCurrentPath
        text: qsTr("Move to trash")
        icon.name: "user-trash"
        sequence: "Del"
        autoRepeat: false
        onTriggered: {
            trashDialogLoader.toDelete = view.currentPath
        }
    }
    MyAction {
        id: actionMetadataPanel
        text: qsTr("Show metadata panel")
        checkable: true
        checked: metadataScroll.visible
        icon.name: "exifinfo"
        sequence: "Ctrl+I"
        autoRepeat: false
        onTriggered: {
            if (appWindow.visibility === Window.FullScreen) {
                metadataScroll.showFullScreen = !metadataScroll.showFullScreen
            } else {
                metadataScroll.showWindowed = !metadataScroll.showWindowed
            }
        }
    }
    MyAction {
        id: actionFullScreen
        text: qsTr("Full screen")
        checkable: true
        checked: appWindow.visibility === Window.FullScreen
        icon.name: "view-fullscreen"
        sequences: ["F11", "Alt+Return"]
        autoRepeat: false
        onTriggered: {
            if (checked) {
                appWindow.wasMaximizedBeforeFullScreen = appWindow.visibility === Window.Maximized
                appWindow.showFullScreen()
            } else if (appWindow.wasMaximizedBeforeFullScreen) {
                appWindow.showMaximized()
                appWindow.showMaximized() // Workaround (tested on KWin 6.8.1)
            } else {
                appWindow.showNormal()
            }
        }
    }
    MyAction {
        id: actionSmoothUpscale
        text: qsTr("Smooth image upscaling")
        checkable: true
        checked: view.smoothUpscale
        icon.name: "blur"
        sequence: "Alt+S"
        autoRepeat: false
        onTriggered: {
            view.smoothUpscale = checked
        }
    }
    MyAction {
        id: actionExit
        text: qsTr("Exit")
        icon.name: "application-exit"
        sequence: "Ctrl+Q"
        autoRepeat: false
        onTriggered: {
            appWindow.close()
        }
    }

    component MyToolTip: Item {
        property bool canDisplay
        property string text

        anchors.fill: parent

        ToolTip.text: text
        ToolTip.visible: canDisplay && text !== ""
        ToolTip.delay: Application.styleHints.toolTipWakeUpDelayAsMSec ?? Application.styleHints.mousePressAndHoldInterval
        ToolTip.toolTip.popupType: ToolTip.Window
    }

    header: ToolBar {
        id: toolBar

        visible: appWindow.visibility !== Window.FullScreen

        implicitHeight: toolBarRow.implicitHeight + 8

        component MyToolButton: ToolButton {
            id: toolButton

            display: Singleton.hasThemeIcon(icon.name)
                     ? AbstractButton.IconOnly
                     : AbstractButton.TextOnly

            icon.width: Defines.ICON_SIZE
            icon.height: Defines.ICON_SIZE

            MyToolTip {
                text: toolButton.text
                canDisplay: toolButton.hovered && toolButton.display === AbstractButton.IconOnly
            }
        }

        Flickable {
            id: toolBarFlickable

            anchors.fill: parent

            boundsMovement: Flickable.StopAtBounds

            contentWidth: toolBarRow.implicitWidth

            RowLayout {
                id: toolBarRow

                anchors.verticalCenter: parent.verticalCenter

                width: toolBarFlickable.width

                MyToolButton { action: actionOpen }
                ToolSeparator {}
                MyToolButton { action: actionPrev }
                MyToolButton { action: actionNext }
                ToolSeparator {}
                MyToolButton { action: actionZoomIn }
                MyToolButton { action: actionZoomOut }
                MyToolButton { action: actionOrigSize }
                MyToolButton { action: actionFitView }
                ToolSeparator {}
                MyToolButton { action: actionRotateCCW }
                MyToolButton { action: actionRotateCW }
                MyToolButton { action: actionHorizontalFlip }
                MyToolButton { action: actionVerticalFlip }
                ToolSeparator {}
                MyToolButton { action: actionReload }
                ToolSeparator {}
                MyToolButton { action: actionMetadataPanel }
                MyToolButton { action: actionFullScreen }

                Item {
                    Layout.fillWidth: true
                    Layout.minimumWidth: toolBarRow.spacing
                }

                MyToolButton {
                    id: menuButton

                    text: qsTr("Menu")

                    icon.name: "application-menu"

                    checkable: menu.popupType === Popup.Item

                    onPressed: {
                        if (!checkable) {
                            down = true
                            menu.popup()
                        }
                    }

                    onClicked: {
                        if (checkable) {
                            if (checked) {
                                menu.popup(menuButton.x, menuButton.y)
                            } else {
                                menu.close()
                            }
                        }
                    }
                }
            }
        }
    }

    View {
        id: view

        anchors.fill: parent

        color: appWindow.visibility === Window.FullScreen ? "black" : "transparent"

        dpr: appWindow.devicePixelRatio
        autoViewIsDefault: appWindow.visibility !== Window.FullScreen

        onParentChanged: {
            visible = false
            if (parent !== appWindow.contentItem) {
                reload()
            }
        }

        ContextMenu.menu: Menu {
            id: menu

            popupType: Singleton.isWayland // Workaround a menu positioning bug on Wayland
                       ? Popup.Item
                       : Popup.Window
            modal: true

            function determineMenuWidth(menu: Menu) {
                let max = 0
                for (let i = 0; i < menu.contentData.length; ++i) {
                    const menuItem = menu.contentData[i] as MenuItem
                    if (menuItem) {
                        const myMenuItem = menu.contentData[i] as MyMenuItem
                        max = Math.max(menuItem.implicitWidth + (myMenuItem ? myMenuItem.shortcutImplicitWidth : 0.0), max)
                    }
                }
                menu.implicitWidth = max
            }

            onAboutToShow: determineMenuWidth(this)

            onClosed: {
                if (menuButton.checkable) {
                    menuButton.checked = false
                } else {
                    menuButton.down = false
                }
            }

            Overlay.modal: Item {}

            component MyMenuItem: MenuItem {
                id: myMenuComponent

                readonly property real shortcutImplicitWidth: myMenuComponentLabel.implicitWidth + myMenuComponentLabel.anchors.leftMargin + myMenuComponentLabel.anchors.rightMargin

                icon.width: Defines.ICON_SIZE
                icon.height: Defines.ICON_SIZE

                Label {
                    id: myMenuComponentLabel
                    anchors {
                        verticalCenter: parent.verticalCenter
                        right: parent.right
                        leftMargin: 16
                        rightMargin: 8
                    }
                    text: {
                        const shortcut = (myMenuComponent.action.shortcut as Shortcut)
                        if (shortcut.sequence !== undefined) {
                            return shortcut.sequence
                        } else {
                            return shortcut.sequences[0]
                        }
                    }
                }
            }

            MyMenuItem { action: actionOpen }
            Menu {
                title: qsTr("Navigation")

                onAboutToShow: menu.determineMenuWidth(this)

                icon.name: "folder-open"
                icon.width: Defines.ICON_SIZE
                icon.height: Defines.ICON_SIZE

                MyMenuItem { action: actionFirst }
                MyMenuItem { action: actionLast }
                MyMenuItem { action: actionPrev }
                MyMenuItem { action: actionNext }
            }
            MenuSeparator {}
            MyMenuItem { action: actionZoomIn }
            MyMenuItem { action: actionZoomOut }
            MyMenuItem { action: actionOrigSize }
            MyMenuItem { action: actionFitView }
            MenuSeparator {}
            MyMenuItem { action: actionHorizontalFlip }
            MyMenuItem { action: actionVerticalFlip }
            MyMenuItem { action: actionRotateCCW }
            MyMenuItem { action: actionRotateCW }
            MenuSeparator {}
            MyMenuItem { action: actionReload }
            MyMenuItem { action: actionTrash }
            MenuSeparator {}
            MyMenuItem { action: actionMetadataPanel }
            MyMenuItem { action: actionFullScreen }
            MenuSeparator {}
            Menu {
                title: qsTr("Settings")

                onAboutToShow: menu.determineMenuWidth(this)

                icon.name: "settings-configure"
                icon.width: Defines.ICON_SIZE
                icon.height: Defines.ICON_SIZE

                MyMenuItem { action: actionSmoothUpscale }
                MenuSeparator {}
                MenuItem {
                    visible: Singleton.displayColorManagementSupported
                    height: visible ? implicitHeight: 0
                    text: qsTr("Display color management")
                    checkable: true
                    checked: appWindow.displayColorManagement
                    icon.name: "color-management"
                    icon.width: Defines.ICON_SIZE
                    icon.height: Defines.ICON_SIZE
                    onTriggered: {
                        appWindow.displayColorManagement = !appWindow.displayColorManagement
                    }
                }
                MenuItem {
                    id: useGammaForSRgbMenuItem
                    visible: Singleton.displayColorManagementSupported
                    height: visible ? implicitHeight: 0
                    enabled: appWindow.displayColorManagement && Singleton.useGammaForSRgbUsable && !Singleton.hasImageColorSpaceOverride
                    text: qsTr("Force gamma 2.2 over sRGB")
                    checkable: true
                    checked: appWindow.useGammaForSRgb
                    icon.name: "video-display"
                    icon.width: Defines.ICON_SIZE
                    icon.height: Defines.ICON_SIZE
                    MyToolTip {
                        text: qsTr("This option has no effect in current configuration")
                        canDisplay: useGammaForSRgbMenuItem.hovered && !useGammaForSRgbMenuItem.enabled
                    }
                    onTriggered: {
                        appWindow.useGammaForSRgb = !appWindow.useGammaForSRgb
                        view.reload()
                    }
                }
                MenuItem {
                    visible: Singleton.hasImageColorSpaceOverride
                    height: visible ? implicitHeight: 0
                    text: qsTr("Reset color space override")
                    icon.name: "view-refresh"
                    icon.width: Defines.ICON_SIZE
                    icon.height: Defines.ICON_SIZE
                    onTriggered: {
                        Singleton.resetImageColorSpaceOverride()
                    }
                }
            }
            MenuSeparator {}
            MyMenuItem { action: actionExit }
        }
    }

    SplitView {
        id: splitView

        anchors.fill: parent

        readonly property real minimumWidth: (metadataScroll.visible ? metadataScroll.SplitView.minimumWidth : 0.0) + viewsItem.SplitView.minimumWidth

        ScrollView {
            id: metadataScroll

            property bool showWindowed: true
            property bool showFullScreen: false

            SplitView.minimumWidth: 100
            SplitView.preferredWidth: 190

            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Component.onCompleted: {
                (metadataScroll.contentItem as Flickable).boundsMovement = Flickable.StopAtBounds
            }

            visible: {
                if (appWindow.visibility === Window.FullScreen) {
                    return showFullScreen
                }
                return showWindowed
            }

            Label {
                id: metadataText

                padding: 8

                width: metadataScroll.width

                wrapMode: Label.Wrap
                textFormat: Text.RichText
            }
        }

        Item {
            id: viewsItem

            SplitView.minimumWidth: 300

            Loader {
                anchors.fill: parent

                active: appWindow.viewerMode === Defines.VIEWER_MODE_CONTAINER

                sourceComponent: WindowContainer {
                    window: Window {
                        Component.onCompleted: {
                            Singleton.setupViewerWindow(this)
                            view.parent = contentItem
                            view.visible = true
                        }
                    }
                }
            }

            Item {
                anchors.fill: parent

                visible: appWindow.viewerMode === Defines.VIEWER_MODE_DIRECT

                onVisibleChanged: {
                    if (visible) {
                        Singleton.setupViewerWindow(null)
                        view.parent = this
                        view.visible = true
                    }
                }
            }
        }
    }

    Loader {
        parent: Overlay.overlay
        anchors.fill: parent

        active: appWindow.viewerMode === Defines.VIEWER_MODE_CONTAINER && menu.popupType === Popup.Item && menu.modal && menu.visible

        sourceComponent: MouseArea {
            acceptedButtons: Qt.AllButtons
            preventStealing: true
            hoverEnabled: true

            onPressed: {
                menu.close()
            }

            onWheel: function(event) {
                event.accepted = true
            }
        }
    }
}
