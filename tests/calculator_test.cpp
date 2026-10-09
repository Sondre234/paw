// SPDX-License-Identifier: GPL-3.0-or-later
// The calculator of the command palette and the start menu's search: parsing, arithmetic,
// conversions, the formatting of results, and what is wrong with what is not an expression.
#include "calculator.hpp"
#include <QRandomGenerator>
#include <QTest>
#include <cmath>
#include <limits>

namespace {
// The result of `text` as it is shown, or "error: WHY".
QString shown(const QString &text) {
    QString error;
    const auto answer = calculator::evaluate(text, &error);
    return answer ? answer->text() : "error: " + error;
}
} // namespace

class CalculatorTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void arithmetic_data() {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("result");
        QTest::newRow("parentheses") << "2*(3+4)" << "14";
        QTest::newRow("power") << "2^10" << "1024";
        QTest::newRow("power with stars") << "2**3" << "8";
        QTest::newRow("root over three") << "sqrt(2)/3" << "0.471404520791";
        QTest::newRow("remainder") << "15 % 4" << "3";
        QTest::newRow("mod") << "15 mod 4" << "3";
        QTest::newRow("exponent and hex") << "1e3 + 0x1f" << "1031";
        QTest::newRow("binary and octal") << "0b101 + 0o17" << "20";
        QTest::newRow("hex digits in capitals") << "0XFF" << "255";
        QTest::newRow("precedence") << "2+3*4" << "14";
        QTest::newRow("left to right") << "10-4-3" << "3";
        QTest::newRow("division left to right") << "100/10/5" << "2";
        QTest::newRow("sign under power") << "-2^2" << "-4";
        QTest::newRow("power from the right") << "2^3^2" << "512";
        QTest::newRow("negative exponent") << "2^-1" << "0.5";
        QTest::newRow("double negation") << "--3" << "3";
        QTest::newRow("unary plus") << "+3 - +2" << "1";
        QTest::newRow("implicit product of parentheses") << "(1+2)(3+4)" << "21";
        QTest::newRow("implicit product of a constant") << "2pi" << "6.28318530718";
        QTest::newRow("implicit product of a function") << "3sqrt(4)" << "6";
        QTest::newRow("two e") << "2e" << "5.43656365692";
        QTest::newRow("implicit product after a space") << "5 pi" << "15.7079632679";
        QTest::newRow("fraction") << "10/4" << "2.5";
        QTest::newRow("leading point") << ".5+.25" << "0.75";
        QTest::newRow("small exponent") << "1.5e-3*2" << "0.003";
        QTest::newRow("signed exponent") << "2E+2" << "200";
        QTest::newRow("factorial") << "5!" << "120";
        QTest::newRow("factorial of nought") << "0!" << "1";
        QTest::newRow("sign before factorial") << "-3!" << "-6";
        QTest::newRow("root sign") << "√16" << "4";
        QTest::newRow("root sign after a number") << "2√4" << "4";
        QTest::newRow("typographic signs") << "3 × 4 ÷ 2 − 1" << "5";
        QTest::newRow("spaces") << "  2 +   2  " << "4";
        QTest::newRow("constants") << "pi + e" << "5.85987448205";
        QTest::newRow("pi sign") << "π/2" << "1.57079632679";
        QTest::newRow("tau") << "tau/pi" << "2";
        QTest::newRow("names in any case") << "SQRT(PI*PI)" << "3.14159265359";
        QTest::newRow("abs") << "abs(-3)" << "3";
        QTest::newRow("round half away") << "round(2.5)" << "3";
        QTest::newRow("round to digits") << "round(3.14159, 2)" << "3.14";
        QTest::newRow("round to tens") << "round(1234, -2)" << "1200";
        QTest::newRow("floor") << "floor(-1.5)" << "-2";
        QTest::newRow("ceil") << "ceil(1.2)" << "2";
        QTest::newRow("trunc") << "trunc(-1.7)" << "-1";
        QTest::newRow("log ten") << "log(1000)" << "3";
        QTest::newRow("log of a base") << "log(8, 2)" << "3";
        QTest::newRow("log two") << "log2(1024)" << "10";
        QTest::newRow("ln") << "ln(e^2)" << "2";
        QTest::newRow("exp") << "exp(0)" << "1";
        QTest::newRow("cube root") << "cbrt(27)" << "3";
        QTest::newRow("min") << "min(3, 1, 2)" << "1";
        QTest::newRow("max") << "max(3, 1, 2)" << "3";
        QTest::newRow("sin of pi is nothing") << "sin(pi)" << "0";
        QTest::newRow("cos of a half pi is nothing") << "cos(pi/2)" << "0";
        QTest::newRow("sin of many pi") << "sin(100pi)" << "0";
        QTest::newRow("cos of pi") << "cos(pi)" << "-1";
        QTest::newRow("tan of a quarter pi") << "tan(pi/4)" << "1";
        QTest::newRow("sin of a sixth pi") << "sin(pi/6)" << "0.5";
        QTest::newRow("tiny sine") << "sin(1e-20)" << "1e-20";
        QTest::newRow("atan") << "4atan(1)" << "3.14159265359";
        QTest::newRow("nested") << "((((1+1))))*(2)" << "4";
        QTest::newRow("large integer") << "2^40" << "1099511627776";
        QTest::newRow("very large") << "2^1000" << "1.07150860719e301";
        QTest::newRow("negative fraction") << "-1/8" << "-0.125";
        QTest::newRow("negative remainder") << "-7 % 3" << "-1";
    }
    void arithmetic() {
        QFETCH(QString, expression);
        QFETCH(QString, result);
        QCOMPARE(shown(expression), result);
    }

    // Results show no crumbs of binary floating point.
    void precision_data() {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("result");
        QTest::newRow("tenths") << "0.1+0.2" << "0.3";
        QTest::newRow("difference") << "1-0.9" << "0.1";
        QTest::newRow("product") << "0.1*3" << "0.3";
        QTest::newRow("cancelling") << "0.1+0.2-0.3" << "0";
        QTest::newRow("cancelling twice") << "1-0.9-0.1" << "0";
        QTest::newRow("hundredths") << "4.35*100" << "435";
        QTest::newRow("near a whole") << "0.1*30" << "3";
        QTest::newRow("thirds") << "1/3" << "0.333333333333";
        QTest::newRow("two thirds") << "2/3" << "0.666666666667";
        QTest::newRow("money") << "19.99*3" << "59.97";
        QTest::newRow("a real difference") << "2-1.9999" << "0.0001";
    }
    void precision() {
        QFETCH(QString, expression);
        QFETCH(QString, result);
        QCOMPARE(shown(expression), result);
    }

    void formatting() {
        QCOMPARE(calculator::format(0), QString("0"));
        QCOMPARE(calculator::format(-0.0), QString("0"));
        QCOMPARE(calculator::format(-42), QString("-42"));
        QCOMPARE(calculator::format(1e15), QString("1000000000000000"));
        QCOMPARE(calculator::format(9007199254740991.0), QString("9007199254740991"));
        QCOMPARE(calculator::format(9007199254740992.0), QString("9.00719925474e15"));
        QCOMPARE(calculator::format(1e20), QString("1e20"));
        QCOMPARE(calculator::format(-1e20), QString("-1e20"));
        QCOMPARE(calculator::format(6.02214076e23), QString("6.02214076e23"));
        QCOMPARE(calculator::format(1.5e-10), QString("1.5e-10"));
        QCOMPARE(calculator::format(1e-5), QString("1e-5"));
        QCOMPARE(calculator::format(0.0001), QString("0.0001"));
        QCOMPARE(calculator::format(123.456), QString("123.456"));
        QCOMPARE(calculator::format(0.30000000000000004), QString("0.3"));
        QCOMPARE(calculator::format(std::sqrt(2.0)), QString("1.41421356237"));
    }

    void conversions_data() {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("result");
        QTest::newRow("kilometres to miles") << "5 km in mi" << "3.10685596119 mi";
        QTest::newRow("fahrenheit to celsius") << "100 f in c" << "37.7777777778 °C";
        QTest::newRow("freezing") << "32 F to C" << "0 °C";
        QTest::newRow("where the scales meet") << "-40 c to f" << "-40 °F";
        QTest::newRow("kelvin") << "0 °C in K" << "273.15 K";
        QTest::newRow("binary to decimal bytes") << "3 GiB in MB" << "3221.225472 MB";
        QTest::newRow("bytes without capitals") << "1 mb in kb" << "1000 kB";
        QTest::newRow("bits") << "1 Mb in kB" << "125 kB";
        QTest::newRow("bits to bytes") << "8 bits in B" << "1 B";
        QTest::newRow("degrees") << "90 deg to rad" << "1.57079632679 rad";
        QTest::newRow("speed") << "100 km/h in mph" << "62.1371192237 mph";
        QTest::newRow("speed to metres a second") << "36 kmh in m/s" << "10 m/s";
        QTest::newRow("time") << "1 day in h" << "24 h";
        QTest::newRow("an expression") << "2 * 3 ft in m" << "1.8288 m";
        QTest::newRow("in in inches") << "1 in in cm" << "2.54 cm";
        QTest::newRow("arrow") << "1 l -> ml" << "1000 ml";
        QTest::newRow("as") << "1 kWh as J" << "3600000 J";
        QTest::newRow("area") << "1 ha in m²" << "10000 m²";
        QTest::newRow("mass") << "1 lb in g" << "453.59237 g";
        QTest::newRow("in capitals") << "5 KM IN MI" << "3.10685596119 mi";
        QTest::newRow("words") << "2 miles to kilometres" << "3.218688 km";
        QTest::newRow("to hex") << "255 in hex" << "0xff";
        QTest::newRow("from hex") << "0xff in dec" << "255";
        QTest::newRow("to binary") << "10 in bin" << "0b1010";
        QTest::newRow("to octal") << "8 to oct" << "0o10";
        QTest::newRow("negative to hex") << "-255 in hex" << "-0xff";
        QTest::newRow("minutes, not min()") << "90 min in h" << "1.5 h";
    }
    void conversions() {
        QFETCH(QString, expression);
        QFETCH(QString, result);
        QCOMPARE(shown(expression), result);
    }
    void conversionParts() {
        const auto answer = calculator::evaluate("5 km in mi");
        QVERIFY(answer);
        QCOMPARE(answer->number, QString("3.10685596119"));
        QCOMPARE(answer->unit, QString("mi"));
        QVERIFY(std::fabs(answer->value - 3.106855961186669) < 1e-12);
    }

    void errors_data() {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("error");
        QTest::newRow("empty") << "" << "Empty";
        QTest::newRow("blank") << "   " << "Empty";
        QTest::newRow("division by zero") << "1/0" << "Division by zero";
        QTest::newRow("remainder by zero") << "5 % 0" << "Division by zero";
        QTest::newRow("nought to a negative power") << "0^-1" << "Division by zero";
        QTest::newRow("root of a negative") << "sqrt(-1)" << "Undefined";
        QTest::newRow("fractional power of a negative") << "(-8)^(1/3)" << "Undefined";
        QTest::newRow("log of nought") << "log(0)" << "Undefined";
        QTest::newRow("log of base one") << "log(5, 1)" << "Undefined";
        QTest::newRow("asin out of range") << "asin(2)" << "Undefined";
        QTest::newRow("tan of a half pi") << "tan(pi/2)" << "Undefined";
        QTest::newRow("unclosed") << "2*(3+4" << "Missing )";
        QTest::newRow("unclosed call") << "sqrt(2" << "Missing )";
        QTest::newRow("unopened") << "2+3)" << "Unexpected “)”";
        QTest::newRow("trailing operator") << "2*" << "Incomplete";
        QTest::newRow("leading operator") << "*2" << "Unexpected “*”";
        QTest::newRow("unknown name") << "foo+1" << "Unknown name “foo”";
        QTest::newRow("a word") << "firefox" << "Unknown name “firefox”";
        QTest::newRow("unknown character") << "2 $ 3" << "Unexpected “$”";
        QTest::newRow("two numbers") << "2 3" << "Unexpected “3”";
        QTest::newRow("bad hex") << "0x1g" << "Unexpected “g”";
        QTest::newRow("factorial of a fraction")
            << "1.5!" << "Factorial of a fraction or a negative number";
        QTest::newRow("factorial of a negative")
            << "(-3)!" << "Factorial of a fraction or a negative number";
        QTest::newRow("factorial too large") << "171!" << "Too large";
        QTest::newRow("overflow") << "10^400" << "Too large";
        QTest::newRow("overflow by product") << "1e300*1e300" << "Too large";
        QTest::newRow("function without parentheses")
            << "sqrt 2" << "“sqrt” needs its argument in parentheses";
        QTest::newRow("too many arguments") << "sqrt(1, 2)" << "“sqrt” takes one argument";
        QTest::newRow("too many for round")
            << "round(1, 2, 3)" << "“round” takes one or two arguments";
        QTest::newRow("round to a fraction of a digit")
            << "round(1, 0.5)" << "round takes a whole number of digits, up to 15";
        QTest::newRow("too deep") << QString(300, '(') + "1" << "Too deeply nested";
        QTest::newRow("unlike units") << "5 km in kg" << "Cannot convert length to mass";
        QTest::newRow("no unit to convert to") << "5 km in" << "Convert to what?";
        QTest::newRow("only a unit") << "5 km" << "Convert to what?";
        QTest::newRow("unknown unit") << "5 foo in m" << "Unknown unit “foo”";
        QTest::newRow("unknown target") << "5 m in foo" << "Unknown unit “foo”";
        QTest::newRow("more after a conversion") << "5 m in ft 2" << "Unexpected “2”";
        QTest::newRow("a fraction in hex")
            << "1.5 in hex" << "Only a whole number can be written in another base";
    }
    void errors() {
        QFETCH(QString, expression);
        QFETCH(QString, error);
        QCOMPARE(shown(expression), "error: " + error);
    }

    // What a search shows: a calculation, not a number or a constant typed to find something.
    void answers() {
        auto value = [](const QString &query) {
            const auto answer = calculator::answer(query);
            return answer ? answer->text() : QString("none");
        };
        QCOMPARE(value("2+2"), QString("4"));
        QCOMPARE(value("2pi"), QString("6.28318530718"));
        QCOMPARE(value("sqrt(16)"), QString("4"));
        QCOMPARE(value("0x1f"), QString("31")); // another base is worth showing
        QCOMPARE(value("5!"), QString("120"));
        QCOMPARE(value("5 km in mi"), QString("3.10685596119 mi"));
        QCOMPARE(value("42"), QString("none"));
        QCOMPARE(value("3.5"), QString("none"));
        QCOMPARE(value("1e3"), QString("none"));
        QCOMPARE(value("-5"), QString("none"));
        QCOMPARE(value("e"), QString("none"));
        QCOMPARE(value("pi"), QString("none"));
        QCOMPARE(value("(7)"), QString("none"));
        QCOMPARE(value("firefox"), QString("none"));
        QCOMPARE(value("2*"), QString("none"));
        QCOMPARE(value("1/0"), QString("none"));
        QCOMPARE(value(""), QString("none"));
        // A leading = asks for anything's value.
        QCOMPARE(value("=pi"), QString("3.14159265359"));
        QCOMPARE(value("= 42"), QString("42"));
        QCOMPARE(value("=1e3"), QString("1000"));
        QCOMPARE(value("="), QString("none"));
        QCOMPARE(value("=firefox"), QString("none"));
    }

    // Nothing typed makes it crash or hang: random text over its alphabet, and long text.
    void survivesAnything() {
        const QString alphabet = "0123456789.eExXbBoO+-*/%^!(),√×÷−→> piasqrtlogminkm°CFhGiB";
        auto *random = QRandomGenerator::global();
        for (int round = 0; round < 20000; ++round) {
            QString text;
            const int length = random->bounded(1, 24);
            for (int i = 0; i < length; ++i)
                text += alphabet[random->bounded(alphabet.size())];
            QString error;
            const auto answer = calculator::evaluate(text, &error);
            QVERIFY2(answer || !error.isEmpty(), qPrintable(text));
            if (answer)
                QVERIFY2(std::isfinite(answer->value), qPrintable(text));
        }
        QString sum = "1";
        for (int i = 0; i < 5000; ++i)
            sum += "+1";
        QCOMPARE(shown(sum), QString("5001"));
        QCOMPARE(shown(QString(5000, '-') + "1"), QString("error: Too deeply nested"));
    }
};

QTEST_GUILESS_MAIN(CalculatorTest)
#include "calculator_test.moc"
