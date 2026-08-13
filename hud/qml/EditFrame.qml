// Der Bearbeiten-Rahmen: sichtbar, solange das HUD ENTSPERRT ist.
//
// Ersetzt die EditGlass-Klasse aus hud/overlay_window.py. Dort war eine eigene
// transparente Scheibe ueber der WebEngineView noetig, weil die alle Mausereignisse
// schluckte. In QML ist das einfach ein Element in derselben Szene.
//
// Die Geometrie-Rechnerei macht bewusst Python (HudController.editTo): dort steht
// sie schon, sie ist an echten Fenstern erprobt, und sie muss ohnehin ans Fenster.

import QtQuick

Item {
    id: editor

    /** Groesse der Anfasser in den Ecken - wie EditGlass.HANDLE. */
    readonly property int handle: 18

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        // Gestrichelt gibt es fuer Rectangle nicht - der laufende Rand macht
        // denselben Punkt ("hier kannst du anfassen") und faellt sogar besser auf.
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.06)
        }
    }

    // Die vier Ecken
    Repeater {
        model: ["tl", "tr", "bl", "br"]
        Rectangle {
            required property string modelData
            width: editor.handle
            height: editor.handle
            color: Theme.accent
            x: modelData.indexOf("l") >= 0 ? 0 : editor.width - editor.handle
            y: modelData.indexOf("t") >= 0 ? 0 : editor.height - editor.handle
        }
    }

    // Hinweiszeile oben
    Rectangle {
        x: editor.handle + 6
        y: 4
        width: hint.implicitWidth + 8
        height: 20
        color: Qt.rgba(0, 0, 0, 0.67)
        Text {
            id: hint
            anchors.centerIn: parent
            text: "Bearbeiten – ziehen = verschieben, Ecken = Größe, Doppelklick = sperren"
            color: "#ffffff"
            font { family: Theme.sans; pixelSize: 12; weight: Font.Bold }
        }
    }

    MouseArea {
        id: grab
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: {
            switch (cornerAt(mouseX, mouseY)) {
            case "tl": case "br": return Qt.SizeFDiagCursor;
            case "tr": case "bl": return Qt.SizeBDiagCursor;
            }
            return Qt.SizeAllCursor;
        }

        function cornerAt(mx, my) {
            var h = editor.handle;
            var left = mx < h, right = mx > editor.width - h;
            var top = my < h, bottom = my > editor.height - h;
            if (top && left) return "tl";
            if (top && right) return "tr";
            if (bottom && left) return "bl";
            if (bottom && right) return "br";
            return "";
        }

        onPressed: function (mouse) {
            var g = mapToGlobal(mouse.x, mouse.y);
            Hud.beginEdit(cornerAt(mouse.x, mouse.y), g.x, g.y);
        }
        onPositionChanged: function (mouse) {
            if (!pressed)
                return;
            var g = mapToGlobal(mouse.x, mouse.y);
            Hud.editTo(g.x, g.y);
        }
        onReleased: Hud.endEdit()
        onDoubleClicked: Hud.locked = true
    }
}
