#include "Almanac.h"

#include <QFile>

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

// The fields of a record, by the start of their name, in the order of a YUMA file
constexpr int kFields = 13;
const char* const kKeys[kFields] = {"id", "health", "eccentricity", "time of applicability", "orbital inclination",
                                    "rate of right ascen", "sqrt(a)", "right ascen at week", "argument of perigee",
                                    "mean anom", "af0", "af1", "week"};

} // namespace

QString readYuma(const QString& path, int nearWeek, std::vector<AlmanacEntry>* out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QStringLiteral("Cannot open %1: %2").arg(path, f.errorString());

    std::vector<AlmanacEntry> sats;
    double v[kFields] = {};
    unsigned seen = 0;
    int startLine = 0;
    const auto finish = [&]() -> QString {
        if (!seen) return QString();
        const QString where = QStringLiteral("The record at line %1").arg(startLine);
        for (int i = 0; i < kFields; ++i)
            if (!(seen & (1u << i))) return QStringLiteral("%1 has no \"%2\" field.").arg(where, QString::fromLatin1(kKeys[i]));
        AlmanacEntry a;
        a.prn = int(v[0]);
        a.health = int(v[1]);
        a.e = v[2];
        a.toa = v[3];
        a.i0 = v[4];
        a.omegaDot = v[5];
        a.sqrtA = v[6];
        a.omega0 = v[7];
        a.omega = v[8];
        a.m0 = v[9];
        a.af0 = v[10];
        a.af1 = v[11];
        a.week = int(v[12]);
        if (a.prn < 1 || a.prn > kNumSats) return QStringLiteral("%1 is for PRN %2: GPS has PRNs 1 to %3.").arg(where).arg(a.prn).arg(kNumSats);
        if (a.e < 0 || a.e >= 0.1 || a.sqrtA < 5000 || a.sqrtA > 5300 || a.toa < 0 || a.toa >= kSecondsPerWeek || a.week < 0)
            return QStringLiteral("%1 (PRN %2) does not hold a GPS orbit.").arg(where).arg(a.prn);
        for (const AlmanacEntry& b : sats)
            if (b.prn == a.prn) return QStringLiteral("PRN %1 is in the almanac twice.").arg(a.prn);
        if (a.week < 1024) a.week += 1024 * std::max(0, int(std::lround((nearWeek - a.week) / 1024.0)));
        sats.push_back(a);
        seen = 0;
        return QString();
    };

    int line = 0;
    while (!f.atEnd()) {
        const QString text = QString::fromLatin1(f.readLine()).trimmed();
        ++line;
        const qsizetype colon = text.indexOf(QLatin1Char(':'));
        if (text.isEmpty() || text.startsWith(QLatin1Char('*')) || colon < 0) continue;
        const QString key = text.left(colon).simplified().toLower();
        const int field = int(std::find_if(kKeys, kKeys + kFields, [&](const char* k) { return key.startsWith(QLatin1String(k)); }) - kKeys);
        if (field == kFields) continue;      // not one of ours
        if (field == 0) {                    // ID starts a record
            const QString error = finish();
            if (!error.isEmpty()) return error;
            startLine = line;
        }
        bool ok = false;
        v[field] = text.mid(colon + 1).trimmed().toDouble(&ok);
        if (!ok) return QStringLiteral("Line %1: \"%2\" is not a number.").arg(line).arg(text.mid(colon + 1).trimmed());
        seen |= 1u << field;
    }
    const QString error = finish();
    if (!error.isEmpty()) return error;
    if (sats.empty()) return QStringLiteral("%1 holds no YUMA almanac.").arg(path);
    std::sort(sats.begin(), sats.end(), [](const AlmanacEntry& a, const AlmanacEntry& b) { return a.prn < b.prn; });
    *out = sats;
    return QString();
}

} // namespace sim
