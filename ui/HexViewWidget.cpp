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

void HexViewWidget::scrollToOffset(int offset) {
  int blockOffset = (offset / 16) * 16;
  QString targetHex = QStringLiteral("%1").arg(blockOffset, 8, 16, QLatin1Char('0')).toUpper();
  
  QTextDocument *doc = m_textEdit->document();
  QTextCursor cursor = doc->find(targetHex);
  
  if (!cursor.isNull()) {
    // Select the specific byte? We can just move the cursor there.
    // The hex display starts after "OFFSET  " (10 chars), and each byte is 3 chars.
    int byteIndex = offset % 16;
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 10 + (byteIndex * 3));
    
    m_textEdit->setTextCursor(cursor);
    m_textEdit->ensureCursorVisible();
  }
}
