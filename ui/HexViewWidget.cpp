#include "HexViewWidget.h"
#include <QFontDatabase>
#include <QLatin1Char>
#include <QString>
#include <QTextCursor>

HexViewWidget::HexViewWidget(QWidget *parent)
    : QWidget(parent), m_textEdit(new QPlainTextEdit(this)) {

  m_textEdit->setReadOnly(true);

  // Use a fixed-pitch font for alignment
  const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  m_textEdit->setFont(fixedFont);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_textEdit);
  setLayout(layout);
}

void HexViewWidget::setBuffer(const uint8_t *data, int size, int sectorIndex) {
  if (!data || size <= 0) {
    m_textEdit->setPlainText(tr("No data available."));
    return;
  }

  QString out;
  if (sectorIndex >= 0) {
    out += QStringLiteral("Sector Index: %1\n").arg(sectorIndex);
    out += QStringLiteral(
        "----------------------------------------------------------\n");
  }

  const int bytesPerLine = 16;
  for (int i = 0; i < size; i += bytesPerLine) {
    // Offset Column (Hex)
    out += QStringLiteral("%1  ").arg(i, 8, 16, QLatin1Char('0')).toUpper();

    QString hexPart;
    QString asciiPart;

    for (int j = 0; j < bytesPerLine; ++j) {
      int idx = i + j;
      if (idx < size) {
        uint8_t b = data[idx];
        hexPart +=
            QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0')).toUpper();
        // GEMDOS/ASCII printable range check
        asciiPart += (b >= 32 && b < 127) ? QChar(b) : QChar('.');
      } else {
        hexPart += QStringLiteral("   ");
        asciiPart += QChar(' ');
      }
    }

    out += hexPart + QStringLiteral("  ") + asciiPart + QLatin1Char('\n');
  }

  m_textEdit->setPlainText(out);
  m_textEdit->moveCursor(QTextCursor::Start);
}

void HexViewWidget::setData(const QByteArray &data) {
  setBuffer(reinterpret_cast<const uint8_t *>(data.constData()), data.size());
}
