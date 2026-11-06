#ifndef MEMORYKEYROW_H
#define MEMORYKEYROW_H

class QHBoxLayout;
class QPushButton;
class QWidget;

// The memory keys between the VFOs: M1-M4 over the MESSAGE bracket, then REC / STORE / RCL with their
// orange sub-labels, centred in a row `rowWidth` wide. MainWindow places the row; MemoryButtonsController
// wires the keys.
namespace MemoryKeyRow {

struct Keys {
    QPushButton *m1 = nullptr;
    QPushButton *m2 = nullptr;
    QPushButton *m3 = nullptr;
    QPushButton *m4 = nullptr;
    QPushButton *rec = nullptr;
    QPushButton *store = nullptr;
    QPushButton *rcl = nullptr;
};

QHBoxLayout *create(QWidget *parent, int rowWidth, Keys &keys);

} // namespace MemoryKeyRow

#endif // MEMORYKEYROW_H
