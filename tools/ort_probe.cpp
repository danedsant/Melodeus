// ort_probe — F0.4: ONNX Runtime con DirectML cargando un modelo trivial.
//
//   ort_probe [ruta_modelo] [iteraciones]
//
// Verifica: registro del EP DirectML (device 0), creación de sesión desde
// memoria, inferencia correcta (ReLU) y tiempo medio por iteración. Sale con
// código 0 si todo OK, 1 en caso contrario.
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <onnxruntime_cxx_api.h>

// dml_provider_factory.h arrastra DirectML.h (fuera de alcance en F0: no se
// enlaza DirectML). Se redeclara el único símbolo requerido; lo exporta
// onnxruntime.dll del paquete Microsoft.ML.OnnxRuntime.DirectML.
extern "C" OrtStatus* ORT_API_CALL
OrtSessionOptionsAppendExecutionProvider_DML(OrtSessionOptions* options, int device_id);

namespace {

int fail(const std::string& msg) {
    std::fprintf(stderr, "[ort] FALLO: %s\n", msg.c_str());
    return 1;
}

std::string take_message(OrtStatus* st) {
    std::string msg = Ort::GetApi().GetErrorMessage(st);
    Ort::GetApi().ReleaseStatus(st);
    return msg;
}

std::vector<char> read_file(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *err = "no se pudo abrir: " + path;
        return {};
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        *err = "modelo vacio: " + path;
    }
    return bytes;
}

} // namespace

int main(int argc, char** argv) {
    const std::string model_path = (argc > 1) ? argv[1] : "external/tools/relu.onnx";
    const int iters = (argc > 2) ? std::atoi(argv[2]) : 200;
    if (iters <= 0) {
        return fail("iteraciones invalidas");
    }

    std::string read_err;
    const std::vector<char> model = read_file(model_path, &read_err);
    if (!read_err.empty()) {
        return fail(read_err);
    }

    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "melodeus-ort-probe");

        std::printf("[ort] proveedores disponibles:\n");
        for (const auto& p : Ort::GetAvailableProviders()) {
            std::printf("  - %s\n", p.c_str());
        }

        Ort::SessionOptions opts;
        opts.DisableMemPattern(); // F5: tensores de entrada con longitud variable
        opts.SetExecutionMode(ORT_SEQUENTIAL);
        opts.SetIntraOpNumThreads(1); // no competir con el hilo de audio

        if (OrtStatus* st = OrtSessionOptionsAppendExecutionProvider_DML(opts, 0)) {
            return fail("registro del EP DirectML: " + take_message(st));
        }
        std::printf("[ort] EP DirectML registrado (device 0)\n");

        Ort::Session session(env, model.data(), model.size(), opts);
        std::printf("[ort] sesion creada desde memoria (%s, %zu bytes)\n", model_path.c_str(),
                    model.size());

        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<float, 64> input{};
        for (size_t i = 0; i < input.size(); ++i) {
            input[i] = (static_cast<float>(i) - 32.0f) / 32.0f;
        }
        std::array<int64_t, 2> shape{1, 64};
        const char* in_names[] = {"x"};
        const char* out_names[] = {"y"};

        auto run_once = [&] {
            auto v = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(), shape.data(),
                                                     shape.size());
            return session.Run(Ort::RunOptions{}, in_names, &v, 1, out_names, 1);
        };

        for (int i = 0; i < 10; ++i) { // calentamiento
            run_once();
        }

        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i) {
            run_once();
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double total_ms =
            std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();

        std::vector<Ort::Value> out = run_once();
        const float* y = out.front().GetTensorMutableData<float>();
        double max_err = 0.0;
        for (size_t i = 0; i < input.size(); ++i) {
            const double want = (input[i] > 0.0f) ? static_cast<double>(input[i]) : 0.0;
            max_err = std::max(max_err, std::fabs(static_cast<double>(y[i]) - want));
        }

        std::printf("[ort] %d inferencias en %.2f ms (promedio %.4f ms/inf)\n", iters, total_ms,
                    total_ms / static_cast<double>(iters));
        std::printf("[ort] verificacion ReLU: max_err=%g %s\n", max_err,
                    (max_err < 1e-6) ? "OK" : "ERROR");
        if (max_err >= 1e-6) {
            return fail("salida incorrecta del modelo");
        }
        std::printf("[ort] ORT=OK\n");
        return 0;
    } catch (const Ort::Exception& e) {
        return fail(e.what());
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}
