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
#include <GLFW/glfw3.h>

// ============================================================================
// Colors
// ============================================================================
static const ImVec4 COL_LINE_NUM   = ImVec4(0.30f, 0.70f, 0.30f, 1.00f);
static const ImVec4 COL_CURSOR_BG  = ImVec4(0.20f, 0.28f, 0.16f, 1.00f);
static const ImVec4 COL_CURSOR_FG  = ImVec4(1.00f, 1.00f, 0.60f, 1.00f);
static const ImVec4 COL_SEARCH_FG  = ImVec4(0.95f, 0.30f, 0.30f, 1.00f);
static const ImVec4 COL_SEARCH_CUR = ImVec4(1.00f, 0.50f, 0.10f, 1.00f);
static const ImVec4 COL_TOOLBAR_BG = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

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
    glfwSwapInterval(1);

    // Store `this` for the static callback
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, keyCallback);

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // NOT enabling NavEnableKeyboard — we handle keys ourselves via GLFW
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding  = 0.0f;
    style.FrameRounding   = 2.0f;
    style.ItemSpacing     = ImVec2(0, 1);
    style.FramePadding    = ImVec2(6, 3);
    style.WindowPadding   = ImVec2(0, 0);
    style.ScrollbarSize   = 12.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);

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
// GLFW key callback → queue
// ============================================================================

void GUI::keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods) {
    GUI* self = static_cast<GUI*>(glfwGetWindowUserPointer(win));
    if (self) self->onKey(key, scancode, action, mods);
}

void GUI::onKey(int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    // When an ImGui InputText is active, let ImGui handle the keys
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;
    keyQueue_.push_back({key, action == GLFW_REPEAT ? 1 : 0});
}

void GUI::processKeyQueue() {
    if (keyQueue_.empty()) return;

    ImGuiIO& io = ImGui::GetIO();
    bool shift = io.KeyShift;

    // Process only the first event per frame to avoid flooding
    KeyEvent ev = keyQueue_.front();
    keyQueue_.clear();

    switch (ev.key) {
    case GLFW_KEY_Q:         viewer_->quit();            break;
    case GLFW_KEY_J:
    case GLFW_KEY_DOWN:      viewer_->moveCursor(1);     break;
    case GLFW_KEY_K:
    case GLFW_KEY_UP:        viewer_->moveCursor(-1);    break;
    case GLFW_KEY_L:
    case GLFW_KEY_RIGHT:     viewer_->scrollHorizontal(3); break;
    case GLFW_KEY_H:
    case GLFW_KEY_LEFT:      viewer_->scrollHorizontal(-3); break;
    case GLFW_KEY_G:
        if (shift) viewer_->goToBottom();
        else       viewer_->goToTop();
        break;
    case GLFW_KEY_PAGE_DOWN: viewer_->loadNextChunk();   break;
    case GLFW_KEY_PAGE_UP:   viewer_->loadPrevChunk();   break;
    case GLFW_KEY_F:         viewer_->toggleFollow();    break;
    case GLFW_KEY_SLASH:     openSearchBar(false);        break;
    case GLFW_KEY_BACKSLASH: openSearchBar(true);         break;
    case GLFW_KEY_SEMICOLON:
        if (shift) openCommandBar();
        break;
    case GLFW_KEY_N:
        viewer_->navigateSearch(shift ? false : true);
        break;
    case GLFW_KEY_R:         viewer_->reloadChunk();     break;
    case GLFW_KEY_0:         viewer_->scrollToLineStart(); break;
    case GLFW_KEY_4:
        if (shift) viewer_->scrollToLineEnd();
        break;
    case GLFW_KEY_ESCAPE:
        if (showSearchBar_)  closeSearchBar(false);
        if (showCommandBar_) closeCommandBar(false);
        break;
    case GLFW_KEY_END:       viewer_->goToBottom();      break;
    case GLFW_KEY_HOME:      viewer_->goToTop();         break;
    default: break;
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

        // Process queued key events (from GLFW callback)
        processKeyQueue();

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

    ImGui::Begin("idit", nullptr, wflags);

    renderMenuBar();
    renderToolbar();

    float bottomH = ImGui::GetFrameHeightWithSpacing() + 4; // status bar
    if (showSearchBar_)  bottomH += ImGui::GetFrameHeightWithSpacing() + 4;
    if (showCommandBar_) bottomH += ImGui::GetFrameHeightWithSpacing() + 4;

    ImGui::BeginChild("##content", ImVec2(0, -bottomH), ImGuiChildFlags_Border,
                       ImGuiWindowFlags_HorizontalScrollbar |
                       ImGuiWindowFlags_AlwaysVerticalScrollbar);
    renderContent();
    ImGui::EndChild();

    if (showSearchBar_)  renderSearchBar();
    if (showCommandBar_) renderCommandBar();
    renderStatusBar();

    ImGui::End();
}

// ---- menu bar ----

void GUI::renderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    ImGui::Text(" %s", viewer_->filePath().c_str());
    ImGui::Separator();
    ImGui::Text(" L%llu–L%llu  %llu KB / %llu KB ",
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

// ---- toolbar ----

void GUI::renderToolbar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_TOOLBAR_BG);
    ImGui::BeginChild("##toolbar", ImVec2(0, ImGui::GetFrameHeightWithSpacing() + 4),
                       ImGuiChildFlags_Border);
    ImGui::PopStyleColor();

    ImGui::SameLine(4);

    // Navigation
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    if (ImGui::ArrowButton("##up", ImGuiDir_Up))    viewer_->moveCursor(-1);
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) viewer_->moveCursor(1);
    ImGui::PopStyleVar();
    ImGui::SameLine(0, 10);

    if (ImGui::Button("Prev Chunk")) viewer_->loadPrevChunk();
    ImGui::SameLine(0, 2);
    if (ImGui::Button("Next Chunk")) viewer_->loadNextChunk();
    ImGui::SameLine(0, 12);

    // Search
    if (ImGui::Button("Find...")) openSearchBar(false);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("< Match")) viewer_->navigateSearch(false);
    ImGui::SameLine(0, 2);
    if (ImGui::Button("Match >")) viewer_->navigateSearch(true);
    ImGui::SameLine(0, 12);

    // Jump
    if (ImGui::Button("Go to Line...")) openCommandBar();
    ImGui::SameLine(0, 12);

    // Top/Bottom
    if (ImGui::Button("Top")) viewer_->goToTop();
    ImGui::SameLine(0, 2);
    if (ImGui::Button("End")) viewer_->goToBottom();
    ImGui::SameLine(0, 12);

    // Follow / Reload
    bool follow = viewer_->followMode();
    if (follow) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.2f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.6f, 0.25f, 0.9f));
    }
    if (ImGui::Button(follow ? "Following" : "Follow")) viewer_->toggleFollow();
    if (follow) {
        ImGui::PopStyleColor(2);
    }
    ImGui::SameLine(0, 2);
    if (ImGui::Button("Reload")) viewer_->reloadChunk();

    ImGui::EndChild();
}

// ---- content area ----

void GUI::renderContent() {
    const auto& lines = viewer_->lines();
    size_t lineCount  = lines.size();
    size_t cursorLine = viewer_->cursorLine();

    if (lineCount == 0) {
        ImGui::TextDisabled("  (empty)");
        return;
    }

    // Auto-scroll in follow mode
    if (viewer_->followMode() && lineCount > 0) {
        float lineH   = ImGui::GetTextLineHeightWithSpacing();
        float targetY = static_cast<float>(cursorLine + 1) * lineH
                        - ImGui::GetWindowHeight() + lineH;
        if (targetY > 0) ImGui::SetScrollY(targetY);
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(lineCount));

    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            size_t idx = static_cast<size_t>(i);
            const std::string& line = lines[idx];
            bool isCursor = (idx == cursorLine);

            ImVec2 lineStart = ImGui::GetCursorScreenPos();
            float   lineH    = ImGui::GetTextLineHeightWithSpacing();

            // Cursor background
            if (isCursor) {
                ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    lineStart,
                    ImVec2(lineStart.x + ImGui::GetWindowWidth(), lineStart.y + lineH),
                    ImGui::ColorConvertFloat4ToU32(COL_CURSOR_BG));
            }

            // Line number
            uint64_t gline = viewer_->globalLine(idx) + 1;
            char lineNum[16];
            snprintf(lineNum, sizeof(lineNum), "%6llu  ", static_cast<unsigned long long>(gline));
            ImGui::PushStyleColor(ImGuiCol_Text, COL_LINE_NUM);
            ImGui::TextUnformatted(lineNum);
            ImGui::PopStyleColor();

            ImGui::SameLine(0, 0);

            // Line content color
            bool hasMatch  = viewer_->isLineSearchMatch(idx);
            bool isCurMatch = viewer_->isCurrentSearchMatch(idx);

            ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (isCurMatch)      textCol = COL_SEARCH_CUR;
            else if (hasMatch)   textCol = COL_SEARCH_FG;
            else if (isCursor)   textCol = COL_CURSOR_FG;

            ImGui::PushStyleColor(ImGuiCol_Text, textCol);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();

            // Click to select line
            ImGui::SetCursorScreenPos(lineStart);
            ImGui::InvisibleButton("##ln", ImVec2(ImGui::GetWindowWidth(), lineH));
            if (ImGui::IsItemClicked()) {
                viewer_->setCursorLine(idx);
            }
        }
    }
    clipper.End();
}

// ---- search bar ----

void GUI::renderSearchBar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushItemWidth(ImGui::GetWindowWidth() - 120);

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                ImGuiInputTextFlags_AutoSelectAll;

    // Focus on first appearance
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }

    if (ImGui::InputText("##search", searchBuf_, sizeof(searchBuf_), flags)) {
        closeSearchBar(true);
    }

    if (ImGui::IsItemEdited()) {
        viewer_->setSearchPattern(searchBuf_);
    }

    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Match count
    if (!viewer_->searchMatches().empty()) {
        ImGui::Text(" %zd/%zu ",
            static_cast<ssize_t>(viewer_->searchCurrentMatch() + 1),
            viewer_->searchMatches().size());
    } else if (!viewer_->searchPattern().empty()) {
        ImGui::TextColored(COL_SEARCH_FG, " no match ");
    }

    ImGui::SameLine();
    if (ImGui::Button(" Cancel ")) closeSearchBar(false);

    ImGui::PopStyleVar();
}

// ---- command bar ----

void GUI::renderCommandBar() {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushItemWidth(140);

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                ImGuiInputTextFlags_AutoSelectAll |
                                ImGuiInputTextFlags_CharsDecimal;

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }

    if (ImGui::InputText("Go to line", commandBuf_, sizeof(commandBuf_), flags)) {
        closeCommandBar(true);
    }

    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button(" Cancel ")) closeCommandBar(false);

    ImGui::PopStyleVar();
}

// ---- status bar ----

void GUI::renderStatusBar() {
    ImGui::Separator();
    if (!viewer_->statusMsg().empty()) {
        ImGui::Text(" %s", viewer_->statusMsg().c_str());
    } else {
        ImGui::TextDisabled(" q:quit  ▲▼:move  ◀▶:scroll  /:find  n/N:match  "
                            "PgUp/Dn:chunk  g/G:top/bot  ::jump  f:follow  r:reload");
    }
}

// ============================================================================
// Search / Command bar toggles
// ============================================================================

void GUI::openSearchBar(bool fullSearch) {
    viewer_->beginSearch(fullSearch);
    showSearchBar_  = true;
    searchBuf_[0]   = '\0';
}

void GUI::closeSearchBar(bool confirm) {
    showSearchBar_ = false;
    if (confirm) {
        viewer_->setSearchPattern(searchBuf_);
        viewer_->confirmSearch();
    } else {
        viewer_->cancelSearch();
    }
    searchBuf_[0] = '\0';
}

void GUI::openCommandBar() {
    viewer_->beginCommand();
    showCommandBar_ = true;
    commandBuf_[0]  = '\0';
}

void GUI::closeCommandBar(bool confirm) {
    showCommandBar_ = false;
    if (confirm && commandBuf_[0] != '\0') {
        try {
            size_t pos = 0;
            uint64_t target = std::stoull(commandBuf_, &pos);
            if (pos == strlen(commandBuf_) && target >= 1) {
                viewer_->jumpToLine(target);
            }
        } catch (...) {}
    }
    viewer_->cancelCommand();
    commandBuf_[0] = '\0';
}
