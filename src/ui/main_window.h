// The main window shell around the Arrangement view: transport bar with
// play/stop, position readout, tempo, and undo/redo driven by the History.
#pragma once

#include <QMainWindow>

#include <filesystem>
#include <functional>
#include <string>

#include "core/history.h"
#include "core/playback.h"

class QCloseEvent;
class QLabel;
class QToolButton;

namespace ui {

class ArrangementView;
class PatternPalette;

// The seam through which the window saves and opens Project files. The
// composition root binds it to the persistence module (architecture-seams
// rule 4: ui links core only), so this widget never learns the file format.
// Each call returns an empty string on success or the error to show.
struct ProjectFilePort {
    std::function<std::string(const std::filesystem::path&)> save;
    std::function<std::string(const std::filesystem::path&)> open;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(core::ProjectHistory& history, core::TransportPort& transport,
               ProjectFilePort files, const QString& product_name, QWidget* parent = nullptr);

protected:
    // Never lose a Project (core document 9.6): unsaved changes are offered a
    // save before the window closes.
    void closeEvent(QCloseEvent* event) override;

private:
    void refresh_transport();
    void refresh_undo_redo();
    void refresh_title();

    // Save to the current file, or ask for one. False when not saved.
    bool save();
    bool save_as();
    void open();
    // Offers Save / Discard / Cancel when dirty. False means cancel.
    bool confirm_discard_changes();

    core::ProjectHistory& history_;
    core::TransportPort& transport_;
    ProjectFilePort files_;
    QString product_name_;
    std::filesystem::path current_file_;  // empty until saved or opened

    ArrangementView* arrangement_view_ = nullptr;
    PatternPalette* palette_ = nullptr;
    QToolButton* play_button_ = nullptr;
    QLabel* position_label_ = nullptr;
    QLabel* tempo_label_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
};

}  // namespace ui
