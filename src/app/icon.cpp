#include "icon.h"

#include <QBitmap>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRegion>
#include <QDebug>

namespace kers::app {

namespace {

// 16/20/24/32 sind die Groessen, die Windows fuer Titelleiste und Tray zieht.
const int kSizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};

QImage square(const QImage& img)
{
    if (img.width() == img.height())
        return img;
    const int side = std::max(img.width(), img.height());
    QImage canvas(side, side, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.drawImage((side - img.width()) / 2, (side - img.height()) / 2, img);
    return canvas;
}

QIcon fromImage(const QImage& img)
{
    const QPixmap base = QPixmap::fromImage(img);
    QIcon icon;
    for (int s : kSizes)
        icon.addPixmap(base.scaled(s, s, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    return icon;
}

QIcon fallback()
{
    // Ersatz-Icon, zur Laufzeit gemalt - haengt an keiner Datei.
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#e10600")));
    p.drawRoundedRect(4, 4, 56, 56, 14, 14);
    p.setPen(QPen(QColor(QStringLiteral("#ffffff"))));
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(40);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("K"));
    p.end();
    return QIcon(pm);
}

}  // namespace

QIcon makeIcon()
{
    static QIcon cached;
    if (!cached.isNull())
        return cached;
    QImage img(QStringLiteral(":/static/brand/Icon.png"));
    if (img.isNull()) {
        qWarning() << "[HUD] Icon fehlt: :/static/brand/Icon.png";
        cached = fallback();
        return cached;
    }
    // Das kleinste Rechteck, das alle nicht-durchsichtigen Punkte enthaelt -
    // QRegion.boundingRect() misst die 1-Bit-Alphamaske in C++ aus.
    const QRegion region(QBitmap::fromImage(img.createAlphaMask()));
    const QRect box = region.boundingRect();
    if (!box.isEmpty())
        img = img.copy(box);
    cached = fromImage(square(img));
    return cached;
}

}  // namespace kers::app
