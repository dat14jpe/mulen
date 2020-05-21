#include "Model.hpp"
#include <vector>
#include <iostream>
#include <glm/glm.hpp>

namespace Mulen::Atmosphere {

    Model::Model()
    {
        // Based on code by Eric Bruneton: https://github.com/ebruneton/precomputed_atmospheric_scattering
        // (don't forget to mention this in the readme! And add the license where appropriate)

        // Values from "Reference Solar Spectral Irradiance: ASTM G-173", ETR column
        // (see http://rredc.nrel.gov/solar/spectra/am1.5/ASTMG173/ASTMG173.html),
        // summed and averaged in each bin (e.g. the value for 360nm is the average
        // of the ASTM G-173 values for all wavelengths between 360 and 370nm).
        // Values in W.m^-2.
        constexpr int kLambdaMin = 360;
        constexpr int kLambdaMax = 830;
        constexpr double kSolarIrradiance[48] = {
            1.11776, 1.14259, 1.01249, 1.14716, 1.72765, 1.73054, 1.6887, 1.61253,
            1.91198, 2.03474, 2.02042, 2.02212, 1.93377, 1.95809, 1.91686, 1.8298,
            1.8685, 1.8931, 1.85149, 1.8504, 1.8341, 1.8345, 1.8147, 1.78158, 1.7533,
            1.6965, 1.68194, 1.64654, 1.6048, 1.52143, 1.55622, 1.5113, 1.474, 1.4482,
            1.41018, 1.36775, 1.34188, 1.31429, 1.28303, 1.26758, 1.2367, 1.2082,
            1.18737, 1.14683, 1.12362, 1.1058, 1.07124, 1.04992
        };
        // Values from http://www.iup.uni-bremen.de/gruppen/molspec/databases/
        // referencespectra/o3spectra2011/index.html for 233K, summed and averaged in
        // each bin (e.g. the value for 360nm is the average of the original values
        // for all wavelengths between 360 and 370nm). Values in m^2.
        constexpr double kOzoneCrossSection[48] = {
            1.18e-27, 2.182e-28, 2.818e-28, 6.636e-28, 1.527e-27, 2.763e-27, 5.52e-27,
            8.451e-27, 1.582e-26, 2.316e-26, 3.669e-26, 4.924e-26, 7.752e-26, 9.016e-26,
            1.48e-25, 1.602e-25, 2.139e-25, 2.755e-25, 3.091e-25, 3.5e-25, 4.266e-25,
            4.672e-25, 4.398e-25, 4.701e-25, 5.019e-25, 4.305e-25, 3.74e-25, 3.215e-25,
            2.662e-25, 2.238e-25, 1.852e-25, 1.473e-25, 1.209e-25, 9.423e-26, 7.455e-26,
            6.566e-26, 5.105e-26, 4.15e-26, 4.228e-26, 3.237e-26, 2.451e-26, 2.801e-26,
            2.534e-26, 1.624e-26, 1.465e-26, 2.078e-26, 1.383e-26, 7.105e-27
        };
        // From https://en.wikipedia.org/wiki/Dobson_unit, in molecules.m^-2.
        constexpr double kDobsonUnit = 2.687e20;
        // Maximum number density of ozone molecules, in m^-3 (computed so at to get
        // 300 Dobson units of ozone - for this we divide 300 DU by the integral of
        // the ozone density profile defined below, which is equal to 15km).
        constexpr double kMaxOzoneNumberDensity = 300.0 * kDobsonUnit / 15000.0;

        constexpr double kRayleigh = 1.24062e-6;
        constexpr double kRayleighScaleHeight = 8000.0;
        constexpr double kMieScaleHeight = 1200.0;
        constexpr double kMieAngstromAlpha = 0.0;
        constexpr double kMieAngstromBeta = 5.328e-3;
        constexpr double kMieSingleScatteringAlbedo = 0.9;
        constexpr double kMiePhaseFunctionG = 0.8;

        std::vector<double>
            wavelengths,
            solarIrradiance,
            rayleighScattering,
            mieScattering,
            mieExtinction,
            absorptionExtinction;
        for (int l = kLambdaMin; l <= kLambdaMax; l += 10)
        {
            double lambda = static_cast<double>(l) * 1e-3;  // micro-meters
            double mie =
                kMieAngstromBeta / kMieScaleHeight * pow(lambda, -kMieAngstromAlpha);
            wavelengths.push_back(l);
            solarIrradiance.push_back(kSolarIrradiance[(l - kLambdaMin) / 10]);
            rayleighScattering.push_back(kRayleigh * pow(lambda, -4));
            mieScattering.push_back(mie * kMieSingleScatteringAlbedo);
            mieExtinction.push_back(mie);
            absorptionExtinction.push_back(kMaxOzoneNumberDensity * kOzoneCrossSection[(l - kLambdaMin) / 10]);
        }

        // - to do: add scale heights for Rayleigh and Mie to model
        
        // Ozone density increases linearly from 0 to 1 between 10 and 25 km, and
        // decreases linearly from 1 to 0 between 25 and 40 km. This is an approximate
        // profile from http://www.kln.ac.lk/science/Chemistry/Teaching_Resources/
        // Documents/Introduction%20to%20atmospheric%20chemistry.pdf (page 10).
        glm::dvec2 absorptionLayer{ 25.0e3, 15.0e3 }; // center, half extent


        auto Interpolate = [&wavelengths](double wavelength, const std::vector<double>& v)
        {
            for (size_t i = 1u; i < wavelengths.size(); ++i)
            {
                if (wavelength > wavelengths[i]) continue;
                return glm::mix(v[i - 1u], v[i], (wavelength - wavelengths[i - 1u]) / (wavelengths[i] - wavelengths[i - 1u]));
            }
            return v.back();
        };
        auto Interpolate3 = [&Interpolate, &wavelengths](const glm::dvec3& lambdas, const std::vector<double>& v)
        {
            return glm::dvec3(Interpolate(lambdas.r, v), Interpolate(lambdas.g, v), Interpolate(lambdas.b, v));
        };

        glm::dvec3 lambdas{ 680.0, 550.0, 440.0 };

        { // display values
            auto betaR = Interpolate3(lambdas, rayleighScattering);
            auto mieS = Interpolate3(lambdas, mieScattering);
            auto mieEx = Interpolate3(lambdas, mieExtinction);
            auto aEx = Interpolate3(lambdas, absorptionExtinction);

            auto disp3 = [](const char* name, const glm::dvec3& v)
            {
                std::cout << name << ": (" << v.r << ", " << v.g << ", " << v.b << ")\n";
            };
            disp3("lambdas", lambdas);
            disp3("betaR", betaR);
            disp3("mieS", mieS);
            disp3("mieEx", mieEx);
            disp3("ozoneEx", aEx);
        }

        // - to do: make use of these values
    }

}
