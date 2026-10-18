// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QImage>
#include <QVariantMap>
#include <QWidget>
namespace Ms {
class SpectrumDisplayControls : public QWidget {
    Q_OBJECT
    QVariantMap _values;
    void save(const QString &, const QVariant &);

  public:
    explicit SpectrumDisplayControls(QWidget *);
    QVariantMap values() const { return _values; }
  signals:
    void changed(bool analysis);
};
QImage colorSpectrum(const QImage &, const QVariantMap &, bool overlay);
} // namespace Ms
