#include "HexViewWidget.h"
#include <QDebug>
#include <QFontDatabase>
#include <QLatin1Char>
#include <QString>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

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

void HexViewWidget::setDiskData(const std::vector<unsigned char> &data) {
  QString html;
  html.reserve(data.size() * 4); // Optimization for P15 RAM

  for (size_t i = 0; i < data.size(); i += 16) {
    // 1. Offset Column
    QString line = QString("%1  ").arg(i, 8, 16, QLatin1Char('0')).toLower();

    // 2. Hex Values Column
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < data.size()) {
        line +=
            QString("%1 ").arg(data[i + j], 2, 16, QLatin1Char('0')).toUpper();
      } else {
        line += "   ";
      }
    }

    line += "  ";

    // 3. ASCII Column
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < data.size()) {
        unsigned char c = data[i + j];
        line += (c >= 32 && c <= 126) ? (char)c : '.';
      }
    }

    html += line + "\n";
  }

  m_textEdit->setPlainText(html); // Use setPlainText for speed

  // Diagnostic verify
  qDebug() << "[HEX] Loaded" << data.size()
           << "bytes into UI. Total lines:" << (data.size() / 16);
}

void HexViewWidget::scrollToOffset(uint32_t offset) {
  uint32_t rowAddress = (offset / 16) * 16;
  QString targetPrefix =
      QStringLiteral("%1").arg(rowAddress, 8, 16, QLatin1Char('0')).toLower();

  QTextDocument *doc = m_textEdit->document();

  // --- DIAGNOSTICS ---
  QTextBlock first = doc->begin();
  QTextBlock last = doc->lastBlock();
  qDebug() << "[DIAG] Search Target:" << targetPrefix;
  qDebug() << "[DIAG] UI Range Start:" << first.text().left(8);
  qDebug() << "[DIAG] UI Range End  :" << last.text().left(8);
  qDebug() << "[DIAG] Total Blocks in UI:" << doc->blockCount();
  // -------------------

  QTextBlock block = doc->begin();
  bool found = false;

  while (block.isValid()) {
    QString text = block.text();
    if (text.startsWith(targetPrefix, Qt::CaseInsensitive)) {
      QTextCursor cursor(block);
      int byteIndex = offset % 16;
      int jumpDistance = 10 + (byteIndex * 3);

      cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor,
                          jumpDistance);
      cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor,
                          2);

      m_textEdit->setTextCursor(cursor);
      m_textEdit->ensureCursorVisible();
      m_textEdit->setFocus();

      qDebug() << "[HEX_JUMP] SUCCESS: Found at block" << block.blockNumber();
      found = true;
      break;
    }
    block = block.next();
  }

  if (!found) {
    qDebug() << "[HEX_JUMP] CRITICAL: Address not in UI buffer.";
  }
}

void HexViewWidget::setData(const QByteArray &data) {
  setBuffer(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
}

void HexViewWidget::scrollToOffset(int offset) {
  if (offset >= 0) {
    scrollToOffset(static_cast<uint32_t>(offset));
  }
}
