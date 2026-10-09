// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/curve.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

void require(bool value, const std::string &what) {
    if (!value)
        throw std::runtime_error("curve invariant failed: " + what);
}
sh_curve parsed(const char *text) {
    sh_curve curve{};
    require(sh_curve_parse(text, &curve), std::string("should parse: ") + text);
    return curve;
}
int main() {
    try {
        const char *names[] = {"linear",  "ease-in",        "ease-out", "ease-in-out",
                               "overshoot", "ease-out-quint", "spring",   "bezier(0.2, 0.9, 0.1, 1)",
                               "bezier(0.42,0,0.58,1)", "  Ease-Out  "};
        for (const char *name : names) {
            auto curve = parsed(name);
            // Ends are exact, out-of-range input is clamped, and it never decreases badly.
            require(sh_curve_eval(&curve, 0) == 0, std::string(name) + " starts at 0");
            require(sh_curve_eval(&curve, 1) == 1, std::string(name) + " ends at 1");
            require(sh_curve_eval(&curve, -3) == 0 && sh_curve_eval(&curve, 9) == 1,
                    std::string(name) + " clamps");
            require(sh_curve_eval(&curve, std::nan("")) == 0, std::string(name) + " NaN");
            for (int i = 1; i < 100; ++i) {
                double value = sh_curve_eval(&curve, i / 100.0);
                require(std::isfinite(value) && value > -0.2 && value < 1.2,
                        std::string(name) + " stays sane");
            }
        }
        // Monotonic curves never step backwards.
        for (const char *name : {"linear", "ease-in", "ease-out", "ease-in-out", "ease-out-quint",
                                 "spring", "bezier(0.2, 0.9, 0.1, 1)"}) {
            auto curve = parsed(name);
            double last = 0;
            for (int i = 1; i <= 1000; ++i) {
                double value = sh_curve_eval(&curve, i / 1000.0);
                require(value >= last - 1e-9, std::string(name) + " is monotonic");
                last = value;
            }
        }
        // Known values.
        auto linear = parsed("linear");
        require(std::fabs(sh_curve_eval(&linear, 0.25) - 0.25) < 1e-12, "linear");
        auto out = parsed("ease-out");
        require(std::fabs(sh_curve_eval(&out, 0.5) - 0.875) < 1e-12, "ease-out is cubic");
        auto inout = parsed("ease-in-out");
        require(std::fabs(sh_curve_eval(&inout, 0.5) - 0.5) < 1e-12, "ease-in-out midpoint");
        auto quint = parsed("ease-out-quint");
        require(sh_curve_eval(&quint, 0.5) > sh_curve_eval(&out, 0.5), "quint is harder");
        auto spring = parsed("spring");
        require(sh_curve_eval(&spring, 0.5) > 0.8, "the spring is mostly there by half time");
        auto overshoot = parsed("overshoot");
        double peak = 0;
        for (int i = 1; i < 100; ++i)
            peak = std::fmax(peak, sh_curve_eval(&overshoot, i / 100.0));
        require(peak > 1.02 && peak < 1.2, "overshoot goes slightly past 1");
        // A bezier with the identity control points is linear.
        auto identity = parsed("bezier(0.333333, 0.333333, 0.666667, 0.666667)");
        for (int i = 0; i <= 10; ++i)
            require(std::fabs(sh_curve_eval(&identity, i / 10.0) - i / 10.0) < 1e-3,
                    "bezier identity");
        // Malformed text is refused and leaves the curve alone.
        sh_curve kept{SH_CURVE_LINEAR, {}};
        for (const char *bad : {"", "fast", "bezier", "bezier(1,2,3)", "bezier(1,2,3,4,5)",
                                "bezier(1.5,0,0,1)", "bezier(-0.1,0,0,1)", "bezier(0,9,0,1)",
                                "bezier(0,0,0,nan)", "bezier(0,0,0,1) x", "ease out",
                                "bezier(0,0,0,1", "spring spring"}) {
            require(!sh_curve_parse(bad, &kept), std::string("should reject: ") + bad);
            require(kept.kind == SH_CURVE_LINEAR, "rejection keeps the curve");
        }
        require(!sh_curve_parse(std::string(500, 'a').c_str(), &kept), "long text");
        std::cout << "Easing curves passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
