#pragma once

#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdint>

class HexViewWidget : public QWidget {
  Q_OBJECT

public:
  explicit HexViewWidget(QWidget *parent = nullptr);


#include <vector>

  void setBuffer(const uint8_t *data, int size, int sectorIndex = -1);
  void setData(const QByteArray &data);
  void setDiskData(const std::vector<unsigned char> &data);
  void scrollToOffset(int offset);
  void scrollToOffset(uint32_t offset);

private:
  QPlainTextEdit *m_textEdit;
};
