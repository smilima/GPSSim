#include "DirectionSpinBox.h"

#include <QLineEdit>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>

DirectionSpinBox::DirectionSpinBox(QWidget* parent) : QDoubleSpinBox(parent)
{
}

void DirectionSpinBox::setDirections(const QString& positive, const QString& negative)
{
    positive_ = positive;
    negative_ = negative;
    setSuffix(QString());      // the unit is part of the text
    lineEdit()->setText(textFromValue(value()));
}

QString DirectionSpinBox::textFromValue(double value) const
{
    const QString number = locale().toString(std::fabs(value), 'f', decimals());
    if (std::fabs(value) < 0.5 * std::pow(10.0, -decimals())) return QStringLiteral("%1 m/s").arg(number);
    return QStringLiteral("%1 m/s %2").arg(number, value < 0 ? negative_ : positive_);
}

// A number, and perhaps a direction or the start of one, in any order
bool DirectionSpinBox::parse(const QString& text, double* value) const
{
    QString t = text.trimmed().toLower();
    t.remove(QStringLiteral("m/s"));
    static const QRegularExpression number(QStringLiteral("[-+]?(\\d+([.,]\\d*)?|[.,]\\d+)"));
    const QRegularExpressionMatch m = number.match(t);
    if (!m.hasMatch()) return false;
    bool ok = false;
    double v = locale().toDouble(m.captured(0), &ok);
    if (!ok) v = QLocale::c().toDouble(QString(m.captured(0)).replace(QLatin1Char(','), QLatin1Char('.')), &ok);
    if (!ok) return false;
    t.remove(m.capturedStart(), m.capturedLength());
    static const QRegularExpression notLetters(QStringLiteral("[^\\p{L}]+"));
    const QStringList words = t.split(notLetters, Qt::SkipEmptyParts);
    if (words.size() > 1) return false;
    if (words.size() == 1) {
        if (positive_.toLower().startsWith(words.front())) v = std::fabs(v);
        else if (negative_.toLower().startsWith(words.front())) v = -std::fabs(v);
        else return false;
    }
    *value = v;
    return true;
}

double DirectionSpinBox::valueFromText(const QString& text) const
{
    double v = 0;
    return parse(text, &v) ? v : value();
}

QValidator::State DirectionSpinBox::validate(QString& text, int&) const
{
    double v = 0;
    if (!parse(text, &v)) return QValidator::Intermediate;      // still being typed
    return v >= minimum() && v <= maximum() ? QValidator::Acceptable : QValidator::Intermediate;
}
