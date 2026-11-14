pragma ComponentBehavior: Bound

// SPDX-FileCopyrightText: 2026 Błażej Szczygieł <mumei6102@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick
import QtQuick.Controls
import "Defines.js" as Defines

Rectangle {
    id: viewItem

    required property real dpr
    required property bool autoViewIsDefault

    readonly property alias currentPath: image.currentPath
    readonly property alias imageZoom: image.zoom

    readonly property bool hasCurrentPath: image.currentPath !== ""
    readonly property bool hasReadyPath: image.readyPath !== ""

    property bool hflipMetadata
    property bool vflipMetadata
    property real rotationMetadata

    property bool hflipUser
    property bool vflipUser
    property real rotationUser

    property alias smoothUpscale: image.smoothUpscale

    function setPath(path: string) {
        priv.setPath(path, false)
    }

    function animationSequence(nextAnimationFrameDelay: int) {
        if (nextAnimationFrameDelay > 0) {
            animationTimer.interval = nextAnimationFrameDelay
            animationTimer.restart()
        } else {
            animationTimer.stop()
        }
    }

    function reload() {
        busyIndicatorTimer.interval = 0
        setPath(image.currentPath)
    }

    function rotate(direction: int) {
        let rotation = rotationUser
        if (direction > 0) {
            rotation += 90.0
            if (rotation >= 360.0) {
                rotation = 0.0
            }
            rotationUser = rotation
        } else if (direction < 0) {
            rotation -= 90.0
            if (rotation < 0.0) {
                rotation = 270.0
            }
        }
        rotationUser = rotation
    }

    function fitToWindow() {
        image.zoom = image.getFitViewZoom()
        image.centerX()
        image.centerY()
        image.autoView = false
        image.fitView = true
    }

    function originalSizeView() {
        image.zoomUnderCursor(1.0)
    }

    function zoomIn() {
        image.zoomUnderCursor(image.getNewZoom(120.0))
    }
    function zoomOut() {
        image.zoomUnderCursor(image.getNewZoom(-120.0))
    }

    QtObject {
        id: priv

        function setPath(path: string, animation: bool) {
            animationTimer.stop()
            image.currentPath = path
            if (path !== "") {
                image.loadingNextAnimationFrame = animation
                image.source = "image://qdre/image/" + Singleton.getImageSourceTriggerSeq(image.sourceTrigger, animation) + "/" + path
                image.sourceTrigger = !image.sourceTrigger
            } else {
                image.source = ""
            }
        }

        function maybeShowScrollBars() {
            if (image.zoom > image.getFitViewZoom()) {
                if (hbar.size < 1.0) {
                    hbar.policy = ScrollBar.AlwaysOn
                    hbar.policy = ScrollBar.AsNeeded
                }
                if (vbar.size < 1.0) {
                    vbar.policy = ScrollBar.AlwaysOn
                    vbar.policy = ScrollBar.AsNeeded
                }
            }
        }
    }

    clip: true

    onWidthChanged: image.reposition()
    onHeightChanged: image.reposition()

    onAutoViewIsDefaultChanged: {
        if (image.isReady) {
            image.autoView = autoViewIsDefault
            if (!autoViewIsDefault) {
                image.fitView = true
            }
            image.reposition()
        }
    }

    Timer {
        id: animationTimer

        onTriggered: {
            priv.setPath(image.currentPath, true)
        }
    }

    Image {
        id: image

        readonly property bool transposed: rotation === 90.0 || rotation === 270.0

        readonly property real viewWidth: transposed ? height : width
        readonly property real viewHeight: transposed ? width : height

        readonly property real translateX: (width - viewWidth) / 2.0
        readonly property real translateY: (height - viewHeight) / 2.0

        readonly property real panMaxX: viewItem.width - viewWidth
        readonly property real panMaxY: viewItem.height - viewHeight

        readonly property bool canScrollX: viewWidth - 1e-9 > viewItem.width
        readonly property bool canScrollY: viewHeight - 1e-9 > viewItem.height
        readonly property bool canScroll: canScrollX || canScrollY

        readonly property bool isLoading: status === Image.Loading
        readonly property bool isReady: status === Image.Ready
        readonly property bool isNoImage: status === Image.Null || status === Image.Error

        readonly property bool loadingBusy: isLoading && !loadingNextAnimationFrame

        readonly property real multiplier: zoom / viewItem.dpr

        property bool loadingNextAnimationFrame

        property real zoom
        property bool smoothUpscale
        property bool fitView
        property bool autoView

        property bool sourceTrigger
        property string currentPath
        property string readyPath

        rotation: (viewItem.rotationUser + viewItem.rotationMetadata) % 360.0

        mirror: viewItem.hflipUser !== viewItem.hflipMetadata
        mirrorVertically: viewItem.vflipUser !== viewItem.vflipMetadata

        asynchronous: true
        retainWhileLoading: true

        cache: false
        mipmap: true

        smooth: (width * viewItem.dpr >= sourceSize.width || height * viewItem.dpr >= sourceSize.height)
                ? zoom === 1.0
                  ? false
                  : smoothUpscale
                : true

        function setSize() {
            width = sourceSize.width * multiplier
            height = sourceSize.height * multiplier
        }

        function getNewZoom(delta: real): real {
            return Math.min(Math.max(0.001, zoom * Math.exp(delta / 1250.0)), 1000.0)
        }

        function getFitViewZoom(): real {
            const sourceWidth = transposed ? sourceSize.height : sourceSize.width
            const sourceHeight = transposed ? sourceSize.width : sourceSize.height
            return viewItem.dpr * Math.min(viewItem.width / sourceWidth, viewItem.height / sourceHeight)
        }

        function reposition() {
            const hasAutoView = autoView
            if ((!hasAutoView && fitView) || (hasAutoView && getFitViewZoom() < 1.0)) {
                viewItem.fitToWindow()
                if (hasAutoView) {
                    fitView = false
                }
            } else if (autoView) {
                viewItem.originalSizeView()
            } else {
                zoomUnderCursor(zoom)
            }
            autoView = hasAutoView
        }

        function centerX() {
            x = (viewItem.width - width) / 2.0
        }
        function centerY() {
            y = (viewItem.height - height) / 2.0
        }

        function zoomUnderCursor(newZoom: real) {
            const ratio = newZoom / zoom
            const oldBboxX = x + translateX
            const oldBboxY = y + translateY

            zoom = newZoom

            if (!canScrollX) {
                centerX()
            }
            if (!canScrollY) {
                centerY()
            }

            if (canScrollX) {
                const lastMousePosX = mouseArea.containsMouse ? mouseArea.lastMousePos.x : mouseArea.width / 2.0
                const bboxX = lastMousePosX - (lastMousePosX - oldBboxX) * ratio
                x = Math.min(Math.max(bboxX, panMaxX), 0.0) - translateX
            }
            if (canScrollY) {
                const lastMousePosY = mouseArea.containsMouse ? mouseArea.lastMousePos.y : mouseArea.height / 2.0
                const bboxY = lastMousePosY - (lastMousePosY - oldBboxY) * ratio
                y = Math.min(Math.max(bboxY, panMaxY), 0.0) - translateY
            }

            fitView = false
            autoView = false

            priv.maybeShowScrollBars()
        }

        onTransposedChanged: reposition()

        onXChanged: {
            hbar.position = Math.min(Math.max(0.0, -(x + translateX) / viewWidth), 1.0 - hbar.visualSize)
        }
        onYChanged: {
            vbar.position = Math.min(Math.max(0.0, -(y + translateY) / viewHeight), 1.0 - vbar.visualSize)
        }

        onIsLoadingChanged: {
            if (!isLoading) {
                loadingNextAnimationFrame = false
            }
        }

        onIsReadyChanged: {
            if (isReady && readyPath !== currentPath) {
                if (viewItem.autoViewIsDefault) {
                    autoView = true
                } else {
                    fitView = true
                }
                viewItem.hflipUser = false
                viewItem.vflipUser = false
                viewItem.rotationUser = 0.0
                readyPath = currentPath
                reposition() // In case the source size doesn't change
            }
        }
        onIsNoImageChanged: {
            if (isNoImage) {
                readyPath = ""
            }
        }

        onSourceSizeChanged: {
            setSize()
            reposition()
        }
        onMultiplierChanged: {
            setSize()
        }
    }

    Label {
        anchors.centerIn: parent

        visible: image.status === Image.Error

        text: visible
              ? qsTr("Unable to load: %1").arg(image.currentPath)
              : ""
    }

    MouseArea {
        id: mouseArea

        property bool exitedWhenRecreatingViewerWindow

        property point lastMousePosBeforeWrap
        property point mouseWrappedPos
        property point lastMousePos

        anchors.fill: parent

        focusPolicy: Qt.StrongFocus

        cursorShape: image.canScroll
                       ? pressed
                         ? Qt.ClosedHandCursor
                         : !cursorTimer.finished
                           ? Qt.OpenHandCursor
                           : Qt.BlankCursor
                       : !cursorTimer.finished
                           ? undefined
                           : Qt.BlankCursor

        acceptedButtons: Qt.LeftButton

        hoverEnabled: true

        function maybeWrapMouse(mousePos: point): point {
            let newMousePos = Qt.point(mousePos.x, mousePos.y)

            const wrapMargin = 2.0

            if (newMousePos.x >= viewItem.width - vbar.width) {
                newMousePos.x = wrapMargin
            } else if (newMousePos.x <= 0.0) {
                newMousePos.x = viewItem.width - vbar.width - wrapMargin
            }

            if (newMousePos.y >= viewItem.height - hbar.height) {
                newMousePos.y = wrapMargin
            } else if (newMousePos.y <= 0.0) {
                newMousePos.y = viewItem.height - hbar.height - wrapMargin
            }

            if (newMousePos !== mousePos)
            {
                Singleton.setViewerCursorPos(mapToGlobal(newMousePos))
                return newMousePos;
            }

            return mousePos
        }

        function cleanUp() {
            mouseWrappedPos = Qt.point(0.0, 0.0)
            lastMousePosBeforeWrap = Qt.point(0.0, 0.0)
        }

        onWheel: function(event) {
            lastMousePos = Qt.point(event.x, event.y)
            image.zoomUnderCursor(image.getNewZoom(event.angleDelta.y))
        }

        onDoubleClicked: function(event) {
            lastMousePos = Qt.point(event.x, event.y)
            if (image.fitView || (image.autoView && image.getFitViewZoom() <= 1.0))
            {
                viewItem.originalSizeView()
            }
            else
            {
                viewItem.fitToWindow()
            }
            cursorTimer.restart()
        }

        onPressed: function(event) {
            lastMousePos = Qt.point(event.x, event.y)
            cursorTimer.restart()
        }
        onPositionChanged: function(event) {
            const newMousePos = Qt.point(event.x, event.y)
            const posDiffers = lastMousePos !== newMousePos
            if (pressed && image.canScroll && posDiffers
                    && (mouseWrappedPos.x <= 0.0 || lastMousePosBeforeWrap.x < newMousePos.x)
                    && (mouseWrappedPos.x >= 0.0 || lastMousePosBeforeWrap.x > newMousePos.x)
                    && (mouseWrappedPos.y <= 0.0 || lastMousePosBeforeWrap.y < newMousePos.y)
                    && (mouseWrappedPos.y >= 0.0 || lastMousePosBeforeWrap.y > newMousePos.y)) {
                const delta = Qt.point(lastMousePos.x - newMousePos.x, lastMousePos.y - newMousePos.y)
                if (image.canScrollX) {
                    image.x = Math.max(Math.min(0.0, image.x + image.translateX - delta.x), image.panMaxX) - image.translateX
                }
                if (image.canScrollY) {
                    image.y = Math.max(Math.min(0.0, image.y + image.translateY - delta.y), image.panMaxY) - image.translateY
                }
                lastMousePosBeforeWrap = Qt.point(newMousePos.x, newMousePos.y);
                lastMousePos = maybeWrapMouse(newMousePos)
                mouseWrappedPos = Qt.point(lastMousePos.x - newMousePos.x, lastMousePos.y - newMousePos.y)
                priv.maybeShowScrollBars()
            } else {
                lastMousePos = newMousePos
            }
            if (posDiffers) {
                if (cursorTimer.distanceToShowAgain > 0) {
                    --cursorTimer.distanceToShowAgain;
                } else {
                    cursorTimer.restart()
                }
            }
        }
        onReleased: {
            cleanUp()
        }
        onCanceled: {
            cleanUp()
        }

        onEntered: {
            if (exitedWhenRecreatingViewerWindow) {
                exitedWhenRecreatingViewerWindow = false
            } else {
                cursorTimer.restart()
            }
        }
        onExited: {
            exitedWhenRecreatingViewerWindow = Singleton.isRecreatingViewerWindow()
            if (!exitedWhenRecreatingViewerWindow) {
                cursorTimer.stop()
            }
        }

        Timer {
            id: cursorTimer

            property int distanceToShowAgain
            property bool finished

            interval: 1250

            onRunningChanged: {
                if (running) {
                    distanceToShowAgain = 0
                    finished = false
                }
            }

            onTriggered: {
                distanceToShowAgain = Defines.CURSOR_DISTANCE_TO_SHOW_AGAIN
                finished = true
            }
        }
    }

    component ViewScrollBar : ScrollBar {
        anchors {
            bottom: parent.bottom
            right: parent.right
        }
        active: hovered || pressed
    }
    ViewScrollBar {
        id: hbar
        anchors.left: parent.left
        orientation: Qt.Horizontal
        size: !image.isNoImage
              ? viewItem.width / image.viewWidth
              : 1.0

        onPositionChanged: {
            if (pressed) {
                image.x = -position * image.viewWidth - image.translateX
            }
        }
    }
    ViewScrollBar {
        id: vbar
        anchors.top: parent.top
        orientation: Qt.Vertical
        size: !image.isNoImage
              ? viewItem.height / image.viewHeight
              : 1.0

        onPositionChanged: {
            if (pressed) {
                image.y = -position * image.viewHeight - image.translateY
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: image.loadingBusy && !busyIndicatorTimer.running
        visible: running

        Timer {
            id: busyIndicatorTimer

            running: image.loadingBusy
            interval: 0

            onTriggered: {
                interval = 125
            }
        }
    }

    DropArea {
        anchors.fill: parent

        onEntered: function(event) {
            event.accepted = Singleton.verifyDropUrls(event.urls)
        }

        onDropped: function(event) {
            event.accepted = Singleton.verifyDropUrls(event.urls)
            if (event.accepted) {
                Singleton.openFile(Singleton.urlToLocalFile(event.urls[0]))
            }
        }
    }

    Drag.active: dragHandler.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.mimeData: {
        "text/uri-list": "file://" + image.readyPath
    }

    DragHandler {
        id: dragHandler

        target: null
        enabled: !image.canScroll && viewItem.hasReadyPath && mouseArea.containsMouse
    }
}
