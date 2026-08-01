#include "gui.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>

// ============================================================================
// Colors
// ============================================================================

static const ImVec4 COL_CURSOR_BG    = ImVec4(0.25f, 0.35f, 0.20f, 1.0f);  // dark yellow-green
static const ImVec4 COL_SEARCH_MATCH = ImVec4(0.80f, 0.20f, 0.20f, 1.0f);  // red
static const ImVec4 COL_SEARCH_CUR   = ImVec4(1.00f, 0.40f, 0.00f, 1.0f);  // orange
static const ImVec4 COL_LINE_NUM     = ImVec4(0.30f, 0.70f, 0.30f, 1.0f);  // green
static const ImVec4 COL_TRUNC_MARK   = ImVec4(0.90f, 0.30f, 0.30f, 1.0f);  // red

// ============================================================================
// Lifecycle
// ============================================================================

GUI::~GUI() {
    shutdown();
}

bool GUI::init() {
    glfwSetErrorCallback([](int, const char* desc) {
        fprintf(stderr, "GLFW error: %s\n", desc);
    });

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return false;
    }

    // OpenGL 3.2 (macOS requires forward compat)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    window_ = glfwCreateWindow(1280, 800, "idit — Log Viewer", nullptr, nullptr);
    if (!window_) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // vsync

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // don't save window layout

    // Style: dark log-viewer look
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 2.0f;
    style.ItemSpacing       = ImVec2(0, 0);
    style.FramePadding      = ImVec2(4, 2);

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    return true;
}

void GUI::shutdown() {
    if (window_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        glfwTerminate();
        window_ = nullptr;
    }
}

// ============================================================================
// Main loop
// ============================================================================

void GUI::run(LogViewer& viewer, const std::string& filepath,
              const ReadOptions& opts, bool followMode) {
    viewer_ = &viewer;

    if (!viewer_->open(filepath, opts)) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n",
                filepath.c_str(), strerror(errno));
        return;
    }

    if (followMode) {
        viewer_->toggleFollow();
    }

    while (!glfwWindowShouldClose(window_) && viewer_->isRunning()) {
        glfwPollEvents();
        viewer_->update();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render();

        ImGui::Render();

        int fbW, fbH;
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }
}

// ============================================================================
// Rendering
// ============================================================================

void GUI::render() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);

    ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("idit", nullptr, wflags);
    ImGui::PopStyleVar();

    renderMenuBar();

    // Compute bottom area height
    float bottomHeight = ImGui::GetFrameHeightWithSpacing(); // status bar
    if (showSearchBar_)  bottomHeight += ImGui::GetFrameHeightWithSpacing() + 4;
    if (showCommandBar_) bottomHeight += ImGui::GetFrameHeightWithSpacing() + 4;

    ImGui::BeginChild("##content", ImVec2(0, -bottomHeight), ImGuiChildFlags_Border,
                       ImGuiWindowFlags_HorizontalScrollbar);
    renderContent();
    ImGui::EndChild();

    if (showSearchBar_)  renderSearchBar();
    if (showCommandBar_) renderCommandBar();
    renderStatusBar();

    ImGui::End();

    handleKeyboardShortcuts();
}

void GUI::renderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    ImGui::Text(" %s ", viewer_->filePath().c_str());

    ImGui::Separator();

    ImGui::Text(" Chunk: L%llu–L%llu  |  %llu KB / %llu KB  ",
                static_cast<unsigned long long>(viewer_->globalLineBase() + 1),
                static_cast<unsigned long long>(viewer_->globalLineBase() + viewer_->lineCount()),
                static_cast<unsigned long long>((viewer_->chunkEnd() - viewer_->chunkStart()) / 1024),
                static_cast<unsigned long long>(viewer_->fileSize() / 1024));

    if (viewer_->followMode()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), " FOLLOW ");
    }

    ImGui::EndMenuBar();
}

void GUI::renderContent() {
    const auto& lines = viewer_->lines();
    size_t lineCount  = lines.size();

    if (lineCount == 0) return;

    // Use ListClipper for efficient rendering of many lines
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(lineCount));

    // Auto-scroll to cursor line when follow mode is active or cursor moves
    size_t cursorLine = viewer_->cursorLine();
    float lineHeight  = ImGui::GetTextLineHeightWithSpacing();

    // Scroll to keep cursor visible
    if (viewer_->followMode()) {
        float targetY = static_cast<float>(cursorLine) * lineHeight;
        float visibleH = ImGui::GetWindowHeight();
        float maxScroll = std::max(0.0f, static_cast<float>(lineCount) * lineHeight - visibleH);
        ImGui::SetScrollY(std::min(targetY, maxScroll));
    }

    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            size_t idx = static_cast<size_t>(i);
            const std::string& line = lines[idx];
            bool isCursor = (idx == cursorLine);

            // Line number
            uint64_t gline = viewer_->globalLine(idx) + 1;
            char lineNum[16];
            snprintf(lineNum, sizeof(lineNum), "%6llu  ", static_cast<unsigned long long>(gline));
            ImGui::PushStyleColor(ImGuiCol_Text, COL_LINE_NUM);
            ImGui::TextUnformatted(lineNum);
            ImGui::PopStyleColor();

            ImGui::SameLine(0, 0);

            // Line content
            bool hasMatch  = viewer_->isLineSearchMatch(idx);
            bool isCurMatch = viewer_->isCurrentSearchMatch(idx);

            ImVec4 textCol;
            if (isCursor) {
                textCol = COL_CURSOR_BG;
            } else if (isCurMatch) {
                textCol = COL_SEARCH_CUR;
            } else if (hasMatch) {
                textCol = COL_SEARCH_MATCH;
            } else {
                textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            }

            // Draw cursor background
            if (isCursor) {
                ImVec2 cursorPos = ImGui::GetCursorScreenPos();
                ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    cursorPos,
                    ImVec2(contentMax.x, cursorPos.y + lineHeight),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(COL_CURSOR_BG.x * 0.5f,
                                                          COL_CURSOR_BG.y * 0.5f,
                                                          COL_CURSOR_BG.z * 0.5f, 0.4f)));
            }

            ImGui::PushStyleColor(ImGuiCol_Text, textCol);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();

            // Click to select line
            if (ImGui::IsItemClicked()) {
                // We can't directly modify cursorLine but we can track it
            }
        }
    }
    clipper.End();
}

void GUI::renderSearchBar() {
    ImGui::PushItemWidth(-1);
    bool enterPressed = ImGui::InputText("##search",
        searchBuf_, sizeof(searchBuf_),
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_AutoSelectAll);

    // Set keyboard focus on first frame
    if (ImGui::IsItemActivated()) {
        // Already focused
    }

    // Sync buffer to viewer
    if (ImGui::IsItemEdited()) {
        viewer_->setSearchPattern(searchBuf_);
    }

    if (enterPressed) {
        deactivateSearchInput(true);
    }

    // Check Escape
    if (ImGui::IsItemDeactivated() && !enterPressed) {
        // Check if Escape was pressed
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            deactivateSearchInput(false);
        }
    }

    ImGui::PopItemWidth();
}

void GUI::renderCommandBar() {
    ImGui::PushItemWidth(-1);
    bool enterPressed = ImGui::InputText("##command",
        commandBuf_, sizeof(commandBuf_),
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_AutoSelectAll |
        ImGuiInputTextFlags_CallbackCharFilter,
        [](ImGuiInputTextCallbackData* data) -> int {
            // Only allow digits
            if (data->EventChar < '0' || data->EventChar > '9') return 1;
            return 0;
        });

    if (ImGui::IsItemActivated()) {
        // Focused
    }

    if (enterPressed) {
        deactivateCommandInput(true);
    }

    if (ImGui::IsItemDeactivated() && !enterPressed) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            deactivateCommandInput(false);
        }
    }

    ImGui::PopItemWidth();
}

void GUI::renderStatusBar() {
    ImGui::Separator();

    if (!viewer_->statusMsg().empty()) {
        ImGui::Text(" %s", viewer_->statusMsg().c_str());
    } else {
        ImGui::Text(" q:quit  j/k:move  h/l:scroll  f:follow  /:search  "
                    "n/N:match  PgUp/Dn:chunk  g/G:top/bot  ::jump  r:reload");
    }
}

// ============================================================================
// Keyboard shortcuts
// ============================================================================

void GUI::handleKeyboardShortcuts() {
    // Don't process shortcuts when ImGui is capturing keyboard (InputText active)
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    // --- Quit ---
    if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
        viewer_->quit();
        return;
    }

    // --- Cursor movement ---
    if (ImGui::IsKeyPressed(ImGuiKey_J) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        viewer_->moveCursor(1);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_K) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        viewer_->moveCursor(-1);
        return;
    }

    // --- Horizontal scroll ---
    if (ImGui::IsKeyPressed(ImGuiKey_L) || ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        viewer_->scrollHorizontal(3);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_H) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        viewer_->scrollHorizontal(-3);
        return;
    }

    // --- Top / Bottom ---
    if (ImGui::IsKeyPressed(ImGuiKey_G)) {
        if (io.KeyShift) {
            viewer_->goToBottom();
        } else {
            viewer_->goToTop();
        }
        return;
    }

    // --- Chunk navigation ---
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
        viewer_->loadNextChunk();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
        viewer_->loadPrevChunk();
        return;
    }

    // --- Follow mode ---
    if (ImGui::IsKeyPressed(ImGuiKey_F)) {
        viewer_->toggleFollow();
        return;
    }

    // --- Search ---
    if (ImGui::IsKeyPressed(ImGuiKey_Slash)) {
        activateSearchInput(false);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backslash)) {
        activateSearchInput(true);
        return;
    }

    // --- Search navigation ---
    if (ImGui::IsKeyPressed(ImGuiKey_N)) {
        if (io.KeyShift) {
            viewer_->navigateSearch(false);
        } else {
            viewer_->navigateSearch(true);
        }
        return;
    }

    // --- Command mode (line jump) ---
    // ':' = Shift + ;
    if (ImGui::IsKeyPressed(ImGuiKey_Semicolon) && io.KeyShift) {
        activateCommandInput();
        return;
    }

    // --- Reload ---
    if (ImGui::IsKeyPressed(ImGuiKey_R)) {
        viewer_->reloadChunk();
        return;
    }

    // --- Scroll to line start ---
    if (ImGui::IsKeyPressed(ImGuiKey_0)) {
        viewer_->scrollToLineStart();
        return;
    }

    // --- Scroll to line end ($ = Shift + 4) ---
    if (ImGui::IsKeyPressed(ImGuiKey_4) && io.KeyShift) {
        viewer_->scrollToLineEnd();
        return;
    }
}

// ============================================================================
// Search / Command bar activation
// ============================================================================

void GUI::activateSearchInput(bool fullSearch) {
    viewer_->beginSearch(fullSearch);
    showSearchBar_ = true;
    searchBuf_[0] = '\0';
}

void GUI::deactivateSearchInput(bool confirm) {
    if (confirm) {
        viewer_->setSearchPattern(searchBuf_);
        viewer_->confirmSearch();
    } else {
        viewer_->cancelSearch();
    }
    showSearchBar_ = false;
    searchBuf_[0] = '\0';
}

void GUI::activateCommandInput() {
    viewer_->beginCommand();
    showCommandBar_ = true;
    commandBuf_[0] = '\0';
}

void GUI::deactivateCommandInput(bool confirm) {
    showCommandBar_ = false;
    if (confirm && commandBuf_[0] != '\0') {
        // Parse and jump
        uint64_t target;
        try {
            size_t pos = 0;
            target = std::stoull(commandBuf_, &pos);
            if (pos == strlen(commandBuf_) && target >= 1) {
                viewer_->jumpToLine(target);
            }
        } catch (...) {}
    }
    viewer_->cancelCommand();
    commandBuf_[0] = '\0';
}
