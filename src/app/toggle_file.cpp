#include "app/toggle_file.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <cstdio>

namespace app {
namespace {

QString toggle_path() {
    // AppConfigLocation is <config>/<applicationName>; main() sets the
    // application name to FCS_APP_ID before anything reads this.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + "/assistant_functions.txt";
}

}  // namespace

assistant::FunctionToggles load_toggles() {
    QFile file(toggle_path());
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QByteArray text = file.readAll();
    return assistant::FunctionToggles::from_text(
        std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
}

bool save_toggles(const assistant::FunctionToggles& toggles) {
    QString path = toggle_path();
    if (!QDir().mkpath(QFileInfo(path).path())) {
        std::fprintf(stderr, "Could not create the settings directory for %s\n",
                     qPrintable(path));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::fprintf(stderr, "Could not write %s: %s\n", qPrintable(path),
                     qPrintable(file.errorString()));
        return false;
    }
    std::string text = toggles.to_text();
    file.write(text.data(), static_cast<qint64>(text.size()));
    if (!file.commit()) {
        std::fprintf(stderr, "Could not save %s: %s\n", qPrintable(path),
                     qPrintable(file.errorString()));
        return false;
    }
    return true;
}

}  // namespace app
