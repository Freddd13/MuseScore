// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QVariantMap>
#include <QWidget>
class QLabel;
namespace Ms {
class SpectrumListenControls : public QWidget {
    Q_OBJECT
    QVariantMap _values;
    QLabel *_frequency;
    void save(const QString &, const QVariant &);

  public:
    explicit SpectrumListenControls(QWidget *);
    QVariantMap values() const { return _values; }
    void setFrequency(double);
  signals:
    void changed();
};
} // namespace Ms
