#pragma once

#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdint>

class HexViewWidget : public QWidget {
  Q_OBJECT

public:
  explicit HexViewWidget(QWidget *parent = nullptr);

  /**
   * @brief setBuffer Updates the display with new binary data.
   * @param data Raw pointer to the data buffer.
   * @param size Size of the data in bytes.
   * @param sectorIndex Optional index to display in the header.
   */
  void setBuffer(const uint8_t *data, int size, int sectorIndex = -1);
  void setData(const QByteArray &data);

private:
  QPlainTextEdit *m_textEdit;
};
