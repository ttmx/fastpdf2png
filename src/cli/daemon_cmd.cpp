// fastpdf2png — Daemon mode (stdin/stdout command loop)
// SPDX-License-Identifier: MIT

#include "cli/daemon_cmd.h"
#include "cli/args.h"
#include "cli/shared_cmd.h"
#include "cli/platform/render_platform.h"
#include "internal/pdfium_render.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "fpdfview.h"

namespace fpdf2png::cli {

int RunDaemon() {
    InitPdfium();

    char line[8192];
    while (std::fgets(line, sizeof(line), stdin)) {
        auto len = std::strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (std::strncmp(line, "QUIT", 4) == 0) break;

        char* tokens[8];
        const auto ntok = SplitTabs(line, tokens, 8);

        if (ntok >= 2 && std::string_view{tokens[0]} == "INFO") {
            auto* doc = FPDF_LoadDocument(tokens[1], nullptr);
            if (!doc) {
                std::printf("ERROR cannot open\n");
            } else {
                std::printf("OK %d\n", FPDF_GetPageCount(doc));
                FPDF_CloseDocument(doc);
            }
            std::fflush(stdout);
            continue;
        }

        if (ntok >= 3 && std::string_view{tokens[0]} == "RENDER") {
            const auto* pdf = tokens[1];
            const auto* pat = tokens[2];
            auto dpi = (ntok >= 4)
                ? static_cast<float>(std::atof(tokens[3])) : 150.0f;
            if (dpi <= 0 || dpi > kMaxDpi) dpi = 150.0f;
            const auto workers = (ntok >= 5)
                ? std::clamp(std::atoi(tokens[4]), 1, kMaxWorkers) : 1;
            const auto comp = (ntok >= 6)
                ? std::clamp(std::atoi(tokens[5]), -1, 2) : -1;
            const auto max_pixels = (ntok >= 7)
                ? static_cast<size_t>(std::strtoull(tokens[6], nullptr, 10)) : 0;

            auto* doc = FPDF_LoadDocument(pdf, nullptr);
            if (!doc) {
                std::printf("ERROR cannot open %s\n", pdf);
                std::fflush(stdout);
                continue;
            }
            const auto pages = FPDF_GetPageCount(doc);

            int oversized_page = -1;
            int oversized_width = 0;
            int oversized_height = 0;
            const auto scale = dpi / internal::kPointsPerInch;
            for (int page_idx = 0; page_idx < pages; ++page_idx) {
                auto* page = FPDF_LoadPage(doc, page_idx);
                if (!page) continue;
                const auto width = static_cast<int>(
                    FPDF_GetPageWidth(page) * scale + 0.5f);
                const auto height = static_cast<int>(
                    FPDF_GetPageHeight(page) * scale + 0.5f);
                FPDF_ClosePage(page);
                if (max_pixels > 0 && width > 0 && height > 0 &&
                    static_cast<size_t>(width) * height > max_pixels) {
                    oversized_page = page_idx;
                    oversized_width = width;
                    oversized_height = height;
                    break;
                }
            }
            FPDF_CloseDocument(doc);

            if (oversized_page >= 0) {
                std::printf("ERROR pixels too large page=%d width=%d height=%d max=%zu\n",
                            oversized_page + 1, oversized_width,
                            oversized_height, max_pixels);
                std::fflush(stdout);
                continue;
            }

            int rc;
            if (workers > 1 && pages > 1) {
                const auto nw = (workers < pages) ? workers : pages;
                rc = RenderMulti(pdf, dpi, pat, pages, nw, comp, false);
            } else {
                rc = RenderSingle(pdf, dpi, pat, pages, comp, false);
            }

            if (rc != 0)
                std::printf("ERROR render failed for %s\n", pdf);
            else
                std::printf("OK %d\n", pages);
            std::fflush(stdout);
            continue;
        }

        std::printf("ERROR unknown command\n");
        std::fflush(stdout);
    }

    FPDF_DestroyLibrary();
    return 0;
}

} // namespace fpdf2png::cli
