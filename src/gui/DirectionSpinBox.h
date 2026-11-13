#pragma once

#include <QDoubleSpinBox>
#include <QString>

// A speed along one axis, shown with its direction instead of a sign: "5.0 m/s east", "5.0 m/s west". "5 w", "5 west",
// "5 m/s west" and "-5" all read as 5 m/s west, and the arrows go through zero into the other direction. The value is
// signed as before: east, north and up are positive.
class DirectionSpinBox : public QDoubleSpinBox
{
    Q_OBJECT

public:
    explicit DirectionSpinBox(QWidget* parent = nullptr);
    void setDirections(const QString& positive, const QString& negative);

protected:
    QString textFromValue(double value) const override;
    double valueFromText(const QString& text) const override;
    QValidator::State validate(QString& text, int& pos) const override;

private:
    bool parse(const QString& text, double* value) const;

    QString positive_ = QStringLiteral("east"), negative_ = QStringLiteral("west");
};
