#include "core/Uuid.h"

#include <QUuid>

std::string Uuid::create() {
    // QUuid::createUuid is documented thread-safe and uses a CSPRNG on all
    // platforms Qt supports; StringFormat::WithoutBraces keeps it plain.
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}
