#include "Location.h"

#include "Gps.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <vector>

namespace sim {

namespace {

constexpr double kDeg = kPi / 180.0;
constexpr double kFoot = 0.3048;

// ---------------------------------------------------------------------------------------------- UTM and MGRS

// The transverse Mercator projection by Krüger's series in the third flattening n. To n^3 it is good to a tenth of a
// millimetre within a UTM zone.
constexpr double kN = kWgsF / (2.0 - kWgsF);
const double kEcc = std::sqrt(kWgsF * (2.0 - kWgsF));
const double kTmRadius = 0.9996 * kWgsA / (1.0 + kN) * (1.0 + kN * kN / 4.0 + kN * kN * kN * kN / 64.0);
const double kAlpha[3] = {kN / 2 - 2 * kN * kN / 3 + 5 * kN * kN * kN / 16, 13 * kN * kN / 48 - 3 * kN * kN * kN / 5,
                          61 * kN * kN * kN / 240};
const double kBeta[3] = {kN / 2 - 2 * kN * kN / 3 + 37 * kN * kN * kN / 96, kN * kN / 48 + kN * kN * kN / 15,
                         17 * kN * kN * kN / 480};

const QLatin1String kBands("CDEFGHJKLMNPQRSTUVWX");      // 8° of latitude each from 80° S; X reaches 84° N
const QLatin1String kMgrsColumns[3] = {QLatin1String("ABCDEFGH"), QLatin1String("JKLMNPQR"), QLatin1String("STUVWXYZ")};
const QLatin1String kMgrsRows("ABCDEFGHJKLMNPQRSTUV");

// East of the central meridian and north of the equator, for a point dLon from the central meridian
void tmForward(double lat, double dLon, double* east, double* north)
{
    const double s = std::sin(lat);
    const double t = std::sinh(std::atanh(s) - kEcc * std::atanh(kEcc * s));      // tangent of the conformal latitude
    const double xi = std::atan2(t, std::cos(dLon)), eta = std::atanh(std::sin(dLon) / std::sqrt(1.0 + t * t));
    double x = eta, y = xi;
    for (int j = 1; j <= 3; ++j) {
        x += kAlpha[j - 1] * std::cos(2 * j * xi) * std::sinh(2 * j * eta);
        y += kAlpha[j - 1] * std::sin(2 * j * xi) * std::cosh(2 * j * eta);
    }
    *east = kTmRadius * x;
    *north = kTmRadius * y;
}

void tmInverse(double east, double north, double* lat, double* dLon)
{
    const double xi = north / kTmRadius, eta = east / kTmRadius;
    double xi1 = xi, eta1 = eta;
    for (int j = 1; j <= 3; ++j) {
        xi1 -= kBeta[j - 1] * std::sin(2 * j * xi) * std::cosh(2 * j * eta);
        eta1 -= kBeta[j - 1] * std::cos(2 * j * xi) * std::sinh(2 * j * eta);
    }
    const double chi = std::asin(std::sin(xi1) / std::cosh(eta1));      // conformal latitude
    double phi = chi;
    for (int i = 0; i < 8; ++i) {      // each pass gains more than two digits
        const double es = kEcc * std::sin(phi);
        phi = 2.0 * std::atan(std::tan(kPi / 4 + chi / 2) * std::pow((1.0 + es) / (1.0 - es), kEcc / 2)) - kPi / 2;
    }
    *lat = phi;
    *dLon = std::atan2(std::sinh(eta1), std::cos(xi1));
}

struct Utm
{
    int zone = 0;
    bool south = false;
    double east = 0, north = 0;      // with the false easting, and the false northing in the south
};

Utm toUtm(double latDeg, double lonDeg)
{
    Utm u;
    u.zone = int(std::floor((lonDeg + 180.0) / 6.0)) % 60 + 1;
    if (latDeg >= 56 && latDeg < 64 && lonDeg >= 3 && lonDeg < 12) u.zone = 32;      // south-west Norway
    if (latDeg >= 72 && lonDeg >= 0 && lonDeg < 42) u.zone = lonDeg < 9 ? 31 : lonDeg < 21 ? 33 : lonDeg < 33 ? 35 : 37;      // Svalbard
    u.south = latDeg < 0;
    tmForward(latDeg * kDeg, std::remainder(lonDeg - (u.zone * 6 - 183), 360.0) * kDeg, &u.east, &u.north);
    u.east += 500000.0;
    if (u.south) u.north += 10000000.0;
    return u;
}

void fromUtm(const Utm& u, double* latDeg, double* lonDeg)
{
    double lat = 0, dLon = 0;
    tmInverse(u.east - 500000.0, u.north - (u.south ? 10000000.0 : 0.0), &lat, &dLon);
    *latDeg = lat / kDeg;
    *lonDeg = std::remainder(u.zone * 6 - 183 + dLon / kDeg, 360.0);
}

int bandOf(double latDeg)
{
    return std::clamp(int(std::floor((latDeg + 80.0) / 8.0)), 0, 19);
}

// How far a latitude is outside a band, in degrees
double outsideBand(int band, double latDeg)
{
    const double low = -80.0 + 8.0 * band, high = band == 19 ? 84.0 : low + 8.0;
    return std::max({low - latDeg, latDeg - high, 0.0});
}

// ---------------------------------------------------------------------------------------------- Writing

// A number with up to so many decimals, without the zeros that end it
QString trimmed(double v, int decimals)
{
    QString s = QString::number(v, 'f', decimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

// An angle as degrees and minutes (2 parts) or degrees, minutes and seconds (3 parts), such as 105° 16' 13.97" W
QString sexagesimal(double deg, int parts, char positive, char negative)
{
    const int decimals = parts == 2 ? 6 : 4;
    const long long perUnit = std::llround(std::pow(10.0, decimals));
    // Counted in the last digit shown, so that 59.99999' carries into the degrees
    long long count = std::llround(std::fabs(deg) * (parts == 2 ? 60.0 : 3600.0) * double(perUnit));
    QString last = trimmed(double(count % (60 * perUnit)) / double(perUnit), decimals);
    if (last.size() == 1 || last.indexOf(QLatin1Char('.')) == 1) last.prepend(QLatin1Char('0'));
    count /= 60 * perUnit;
    const QString whole = parts == 2 ? QStringLiteral("%1° ").arg(count)
                                     : QStringLiteral("%1° %2' ").arg(count / 60).arg(count % 60, 2, 10, QLatin1Char('0'));
    return whole + last + QLatin1Char(parts == 2 ? '\'' : '"') + QLatin1Char(' ') + QLatin1Char(deg < 0 ? negative : positive);
}

// ---------------------------------------------------------------------------------------------- Reading

struct Token
{
    enum Kind { Number, Word, Degree, Minute, Second, Colon };
    Kind kind = Number;
    QString text;          // a number as written, or a word in capitals
    double value = 0;
};

// Splits a position into numbers, words and the marks of an angle
QString tokenize(const QString& s, std::vector<Token>* out)
{
    const auto digit = [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); };
    for (qsizetype i = 0; i < s.size();) {
        const QChar c = s[i];
        const char16_t u = c.unicode();
        if (c.isSpace() || QStringView(u",;=/()[]").contains(c)) {
            ++i;
            continue;
        }
        Token t;
        const bool minus = u == u'-' || u == 0x2212;
        if (digit(c) || u == u'.' || u == u'+' || minus) {
            const qsizetype from = i + (digit(c) || u == u'.' ? 0 : 1);
            qsizetype j = from;
            while (j < s.size() && (digit(s[j]) || s[j] == QLatin1Char('.'))) ++j;
            bool ok = false;
            t.value = s.mid(from, j - from).toDouble(&ok);
            if (!ok) return QStringLiteral("Cannot read \"%1\" as a number.").arg(s.mid(i, std::max<qsizetype>(j - i, 1)));
            if (minus) t.value = -t.value;
            t.text = (minus ? QStringLiteral("-") : QString()) + s.mid(from, j - from);
            i = j;
        } else if (u == 0x00B0 || u == 0x00BA || u == 0x02DA) {      // ° and the signs typed in its place
            t.kind = Token::Degree;
            ++i;
        } else if (u == u'\'' || u == 0x2032 || u == 0x2019 || u == 0x2018 || u == 0x00B4 || u == u'`') {
            ++i;
            if (!out->empty() && out->back().kind == Token::Minute && s[i - 2] == c) {      // '' for "
                out->back().kind = Token::Second;
                continue;
            }
            t.kind = Token::Minute;
        } else if (u == u'"' || u == 0x2033 || u == 0x201D || u == 0x201C) {
            t.kind = Token::Second;
            ++i;
        } else if (u == u':') {
            t.kind = Token::Colon;
            ++i;
        } else if (c.isLetter()) {
            qsizetype j = i;
            while (j < s.size() && s[j].isLetter() && s[j].unicode() != 0x00BA) ++j;
            t.kind = Token::Word;
            t.text = s.mid(i, j - i).toUpper();
            i = j;
        } else {
            return QStringLiteral("Cannot read \"%1\" in a position.").arg(c);
        }
        out->push_back(t);
    }
    return QString();
}

bool lengthUnit(const Token& t, double* metres)
{
    static const QStringList metre{QStringLiteral("M"), QStringLiteral("METER"), QStringLiteral("METERS"), QStringLiteral("METRE"),
                                   QStringLiteral("METRES")};
    static const QStringList foot{QStringLiteral("FT"), QStringLiteral("FEET"), QStringLiteral("FOOT")};
    if (t.kind != Token::Word || (!metre.contains(t.text) && !foot.contains(t.text))) return false;
    *metres = foot.contains(t.text) ? kFoot : 1.0;
    return true;
}

// The height that may end a position, from token i on: a number, in metres unless feet follow. False when something
// else stands there.
bool heightAtEnd(const std::vector<Token>& t, size_t i, double* h)
{
    if (i == t.size()) return true;
    double unit = 1.0;
    if (t[i].kind != Token::Number || t.size() - i > 2 || (t.size() - i == 2 && !lengthUnit(t[i + 1], &unit))) return false;
    *h = t[i].value * unit;
    return true;
}

bool isDigits(const Token& t)
{
    return t.kind == Token::Number && std::all_of(t.text.begin(), t.text.end(), [](QChar c) { return c.isDigit(); });
}

bool isZone(const Token& t)
{
    return isDigits(t) && t.text.size() <= 2;
}

// 13TDE7691229455 or 13T DE 76912 29455: a UTM zone and band, a 100 km square, then the easting and the northing
// within it, each to the same count of digits
QString parseMgrs(const std::vector<Token>& t, bool* matched, double* latDeg, double* lonDeg, double* h)
{
    *matched = false;
    if (t.empty() || !isZone(t[0])) return QString();
    QString letters;
    size_t i = 1;
    while (i < t.size() && t[i].kind == Token::Word && letters.size() < 3) letters += t[i++].text;
    if (letters.size() != 3) return QString();
    size_t numbers = 0;
    while (i + numbers < t.size() && t[i + numbers].kind == Token::Number) ++numbers;
    double unit = 1.0;
    const bool hasUnit = i + numbers + 1 == t.size() && lengthUnit(t.back(), &unit);
    if ((i + numbers != t.size() && !hasUnit) || numbers > 3 || (hasUnit && numbers == 0)) return QString();
    *matched = true;

    // Which of the numbers are the digits, and which the height
    const bool pair = numbers >= 2 && isDigits(t[i]) && isDigits(t[i + 1]) && t[i].text.size() == t[i + 1].text.size();
    const size_t used = numbers == 3 || (numbers == 2 && pair && !hasUnit) ? 2 : (numbers == 2 || (numbers == 1 && !hasUnit)) ? 1 : 0;
    QString digits;
    bool plain = true;
    for (size_t k = 0; k < used; ++k) {
        plain = plain && isDigits(t[i + k]);
        digits += t[i + k].text;
    }
    if (!plain || digits.size() % 2 || digits.size() > 10 || (used == 2 && !pair))
        return QStringLiteral("An MGRS position takes the easting then the northing, each to the same count of digits, up to five.");
    const int zone = int(t[0].value);
    const int band = int(kBands.indexOf(letters[0]));
    const int column = zone < 1 || zone > 60 ? -1 : int(kMgrsColumns[(zone - 1) % 3].indexOf(letters[1]));
    int row = int(kMgrsRows.indexOf(letters[2]));
    if (band < 0 || column < 0 || row < 0) return QStringLiteral("MGRS has no square %1%2.").arg(zone).arg(letters);
    row = (row + 20 - (zone % 2 ? 0 : 5)) % 20;

    const qsizetype half = digits.size() / 2;
    const double step = std::pow(10.0, 5 - int(half));
    Utm u;
    u.zone = zone;
    u.south = band < 10;
    u.east = (column + 1) * 100000.0 + digits.left(half).toDouble() * step;
    const double north = row * 100000.0 + digits.mid(half).toDouble() * step;
    // The row letters come round every 2000 km: the band tells which round this is
    double off = 1e9;
    for (int round = 0; round < 5; ++round) {
        double lat = 0, lon = 0;
        u.north = north + round * 2000000.0;
        fromUtm(u, &lat, &lon);
        if (outsideBand(band, lat) < off) {
            off = outsideBand(band, lat);
            *latDeg = lat;
            *lonDeg = lon;
        }
    }
    if (off > 1.0) return QStringLiteral("Square %1 is not in band %2 of zone %3.").arg(letters.mid(1)).arg(letters[0]).arg(zone);
    if (numbers > used) *h = t[i + used].value * unit;
    return QString();
}

// 13T 476912 4429455, 13T 476912mE 4429455mN or 13 North 476912 4429455
QString parseUtm(const std::vector<Token>& t, bool* matched, double* latDeg, double* lonDeg, double* h)
{
    *matched = false;
    if (t.size() < 4 || !isZone(t[0]) || t[1].kind != Token::Word) return QString();
    const QString& side = t[1].text;
    const bool word = side == QLatin1String("NORTH") || side == QLatin1String("SOUTH");
    if (!word && side.size() != 1) return QString();
    size_t i = 2;
    const auto number = [&](char letter, double* v) {      // a number, and the m, E or N that may follow it
        if (i >= t.size() || t[i].kind != Token::Number) return false;
        *v = t[i++].value;
        if (i < t.size() && t[i].kind == Token::Word
            && (t[i].text == QLatin1String("M") || t[i].text == QString(QLatin1Char(letter)) || t[i].text == QLatin1Char('M') + QString(QLatin1Char(letter))))
            ++i;
        return true;
    };
    Utm u;
    double height = *h;
    if (!number('E', &u.east) || !number('N', &u.north) || u.east < 1000 || !heightAtEnd(t, i, &height)) return QString();
    *matched = true;

    u.zone = int(t[0].value);
    if (u.zone < 1 || u.zone > 60) return QStringLiteral("UTM zones run from 1 to 60.");
    if (u.east < 100000 || u.east >= 1000000 || u.north < 0 || u.north > 10000000)
        return QStringLiteral("A UTM easting runs from 100000 to 999999 m, and a northing from 0 to 10000000 m.");
    const int band = word ? -1 : int(kBands.indexOf(side[0]));
    if (!word && band < 0) return QStringLiteral("%1 is not a UTM latitude band: they run from C to X, without I and O.").arg(side);
    u.south = word ? side == QLatin1String("SOUTH") : band < 10;
    fromUtm(u, latDeg, lonDeg);
    if (!word && outsideBand(band, *latDeg) > 1e-4) {
        // N and S are also written for the hemisphere
        if (side == QLatin1String("S")) {
            u.south = true;
            fromUtm(u, latDeg, lonDeg);
        } else if (side != QLatin1String("N")) {
            return QStringLiteral("%1 m north in zone %2 is in band %3, not %4.")
                .arg(trimmed(u.north, 2)).arg(u.zone).arg(kBands[bandOf(*latDeg)]).arg(side);
        }
    }
    *h = height;
    return QString();
}

// X, Y and Z from the centre of the Earth, in metres: three numbers that far out, or marked x, y and z
bool parseEcef(const std::vector<Token>& t, double* latDeg, double* lonDeg, double* h)
{
    double xyz[3] = {};
    bool marked = false;
    size_t i = 0;
    for (int k = 0; k < 3; ++k) {
        if (i < t.size() && t[i].kind == Token::Word && t[i].text == QString(QLatin1Char("XYZ"[k]))) {
            marked = true;
            ++i;
        }
        if (i >= t.size() || t[i].kind != Token::Number) return false;
        xyz[k] = t[i++].value;
        double unit = 1.0;
        if (i < t.size() && lengthUnit(t[i], &unit)) {
            xyz[k] *= unit;
            ++i;
        }
    }
    if (i != t.size() || (!marked && std::sqrt(xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2]) < 1e6)) return false;
    double lat = 0, lon = 0;
    ecefToLlh({xyz[0], xyz[1], xyz[2]}, &lat, &lon, h);
    *latDeg = lat / kDeg;
    *lonDeg = lon / kDeg;
    return true;
}

// A number of an angle
struct Part
{
    double value = 0;
    bool negative = false;
    int mark = 0;            // what follows it: 1 for °, 2 for ', 3 for ", 0 for none
    bool tied = false;       // a colon ties it to the next
};

// One angle of up to three parts: degrees, minutes and seconds
bool angle(const std::vector<Part>& parts, double* deg)
{
    double v = std::fabs(parts[0].value);
    for (size_t i = 0; i < parts.size(); ++i) {
        const Part& p = parts[i];
        if (p.mark && p.mark != int(i) + 1) return false;                              // ° ' " in that order
        if (i > 0 && (p.negative || p.value >= 60)) return false;
        if (i + 1 < parts.size() && p.value != std::floor(p.value)) return false;      // decimals in the last part only
        if (i > 0) v += p.value / (i == 1 ? 60.0 : 3600.0);
    }
    *deg = parts[0].negative ? -v : v;
    return true;
}

// The latitude then the longitude, each as degrees, as degrees and minutes, or as degrees, minutes and seconds
QString parseAngles(const std::vector<Token>& t, bool heightGiven, double* latDeg, double* lonDeg, double* h, LocationFormat* format)
{
    const QString usage = QStringLiteral("Write the latitude then the longitude, such as 40.0149, -105.2705 or 40° 00.9' N, 105° 16.2' W "
                                         "or 40° 00' 53.9\" N, 105° 16' 14\" W.");
    static const QStringList sides{QStringLiteral("N"), QStringLiteral("S"), QStringLiteral("E"), QStringLiteral("W"),
                                   QStringLiteral("NORTH"), QStringLiteral("SOUTH"), QStringLiteral("EAST"), QStringLiteral("WEST")};
    // The numbers, with the mark after each, and where N, S, E and W stand among them
    std::vector<Part> parts;
    QString letters;
    std::vector<size_t> letterAt;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i].kind == Token::Word) {
            if (!sides.contains(t[i].text)) return QStringLiteral("Cannot read \"%1\" in a position.").arg(t[i].text);
            letters += t[i].text[0];
            letterAt.push_back(parts.size());
        } else if (t[i].kind == Token::Number) {
            Part p;
            p.value = std::fabs(t[i].value);
            p.negative = t[i].text.startsWith(QLatin1Char('-'));
            if (i + 1 < t.size() && t[i + 1].kind != Token::Number && t[i + 1].kind != Token::Word) {
                const Token::Kind next = t[++i].kind;
                p.tied = next == Token::Colon;
                p.mark = next == Token::Degree ? 1 : next == Token::Minute ? 2 : next == Token::Second ? 3 : 0;
            }
            parts.push_back(p);
        } else {
            return usage;
        }
    }

    // Which numbers make the first angle, which the second, and what is left for the height
    std::vector<Part> a, b, rest;
    const size_t n = parts.size();
    const auto take = [&](std::vector<Part>* to, size_t from, size_t end) { to->assign(parts.begin() + from, parts.begin() + end); };
    const auto degrees = [](const Part& p) { return p.mark == 1; };
    if (letters.size() == 2 && letterAt[0] == 0) {             // N 40 ... W 105 ...
        take(&a, 0, letterAt[1]);
        take(&b, letterAt[1], n);
    } else if (letters.size() == 2) {                          // 40 ... N 105 ... W
        take(&a, 0, letterAt[0]);
        take(&b, letterAt[0], letterAt[1]);
        take(&rest, letterAt[1], n);
    } else if (!letters.isEmpty()) {
        return QStringLiteral("Give N or S for the latitude and E or W for the longitude, or neither.");
    } else if (std::any_of(parts.begin(), parts.end(), degrees)) {      // each ° starts an angle
        const size_t second = n < 2 ? n : size_t(std::find_if(parts.begin() + 1, parts.end(), degrees) - parts.begin());
        take(&a, 0, second);
        take(&b, second, n);
    } else if (std::any_of(parts.begin(), parts.end(), [](const Part& p) { return p.tied; })) {      // 40:00:53.9
        size_t i = 0;
        const auto run = [&](std::vector<Part>* to) {
            const size_t from = i;
            while (i < n && parts[i].tied) ++i;
            if (i < n) ++i;
            take(to, from, i);
        };
        run(&a);
        run(&b);
        take(&rest, i, n);
    } else {
        take(&a, 0, n / 2);
        take(&b, n / 2, n / 2 * 2);
        take(&rest, n / 2 * 2, n);
    }
    if (rest.empty() && b.size() == a.size() + 1) {
        rest.push_back(b.back());
        b.pop_back();
    }
    if (a.empty() || a.size() > 3 || a.size() != b.size() || rest.size() > 1) return usage;
    if (!rest.empty() && (heightGiven || rest[0].mark || rest[0].tied)) return usage;

    double first = 0, second = 0;
    if (!angle(a, &first) || !angle(b, &second)) return usage;
    if (letters.size() == 2) {
        const auto south = [](QChar c) { return c == QLatin1Char('S') || c == QLatin1Char('W'); };
        const auto latitude = [](QChar c) { return c == QLatin1Char('N') || c == QLatin1Char('S'); };
        if (latitude(letters[0]) == latitude(letters[1])) return QStringLiteral("Give N or S for the latitude and E or W for the longitude.");
        first = south(letters[0]) ? -std::fabs(first) : std::fabs(first);
        second = south(letters[1]) ? -std::fabs(second) : std::fabs(second);
        if (!latitude(letters[0])) std::swap(first, second);
    }
    if (std::fabs(first) > 90) return QStringLiteral("The latitude comes first, and runs from -90° to 90°.");
    if (second > 180 && second <= 360) second -= 360;
    if (std::fabs(second) > 180) return QStringLiteral("The longitude runs from -180° to 180°.");
    *latDeg = first;
    *lonDeg = second;
    if (!rest.empty()) *h = rest[0].negative ? -rest[0].value : rest[0].value;
    *format = a.size() == 1 ? LocationFormat::Degrees : a.size() == 2 ? LocationFormat::DegreesMinutes : LocationFormat::DegreesMinutesSeconds;
    return QString();
}

} // namespace

QString formatLocation(LocationFormat format, double latDeg, double lonDeg, double h)
{
    const QString height = QStringLiteral(", %1 m").arg(trimmed(h, 3));
    const QString between = QStringLiteral(", ");
    const bool inGrid = latDeg >= -80.0 && latDeg <= 84.0;
    if (format == LocationFormat::Ecef) {
        const Vec3 p = llhToEcef(latDeg * kDeg, lonDeg * kDeg, h);
        return trimmed(p.x, 2) + between + trimmed(p.y, 2) + between + trimmed(p.z, 2) + QStringLiteral(" m");
    }
    if (format == LocationFormat::DegreesMinutes || format == LocationFormat::DegreesMinutesSeconds) {
        const int parts = format == LocationFormat::DegreesMinutes ? 2 : 3;
        return sexagesimal(latDeg, parts, 'N', 'S') + between + sexagesimal(lonDeg, parts, 'E', 'W') + height;
    }
    if (format == LocationFormat::Utm && inGrid) {
        const Utm u = toUtm(latDeg, lonDeg);
        return QStringLiteral("%1%2 %3 %4").arg(u.zone).arg(kBands[bandOf(latDeg)]).arg(trimmed(u.east, 2), trimmed(u.north, 2)) + height;
    }
    if (format == LocationFormat::Mgrs && inGrid) {
        const Utm u = toUtm(latDeg, lonDeg);
        // A square holds the metre it starts at. The margin keeps a position read from MGRS in its square.
        const long long east = (long long)std::floor(u.east + 0.005), north = (long long)std::floor(u.north + 0.005);
        const QLatin1Char column = kMgrsColumns[(u.zone - 1) % 3][std::clamp(int(east / 100000) - 1, 0, 7)];
        const QLatin1Char row = kMgrsRows[int((north / 100000 + (u.zone % 2 ? 0 : 5)) % 20)];
        return QStringLiteral("%1%2%3%4 %5 %6").arg(u.zone).arg(kBands[bandOf(latDeg)]).arg(column).arg(row)
                   .arg(east % 100000, 5, 10, QLatin1Char('0')).arg(north % 100000, 5, 10, QLatin1Char('0')) + height;
    }
    return trimmed(latDeg, 8) + between + trimmed(lonDeg, 8) + height;
}

QString parseLocation(const QString& text, double* latDeg, double* lonDeg, double* h, LocationFormat* format)
{
    std::vector<Token> t;
    QString error = tokenize(text, &t);
    if (!error.isEmpty()) return error;

    double lat = 0, lon = 0, height = *h;
    LocationFormat f = LocationFormat::Mgrs;
    bool matched = false;
    error = parseMgrs(t, &matched, &lat, &lon, &height);
    if (!matched) {
        f = LocationFormat::Utm;
        error = parseUtm(t, &matched, &lat, &lon, &height);
    }
    if (!matched && parseEcef(t, &lat, &lon, &height)) {
        f = LocationFormat::Ecef;
    } else if (!matched) {
        double unit = 1.0;
        const bool heightGiven = t.size() >= 2 && t[t.size() - 2].kind == Token::Number && lengthUnit(t.back(), &unit);
        if (heightGiven) {
            height = t[t.size() - 2].value * unit;
            t.resize(t.size() - 2);
        }
        error = parseAngles(t, heightGiven, &lat, &lon, &height, &f);
    }
    if (!error.isEmpty()) return error;
    *latDeg = lat;
    *lonDeg = lon;
    *h = height;
    if (format) *format = f;
    return QString();
}

} // namespace sim
