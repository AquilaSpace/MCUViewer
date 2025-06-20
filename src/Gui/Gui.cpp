#include "Gui.hpp"

#include <imgui.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "PlotHandler.hpp"
#include "Statistics.hpp"
#include "StlinkDebugProbe.hpp"
#include "glfw3.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#ifdef USE_EMBEDDED_FONT
#include "font_data.h"
#endif

#ifdef _WIN32
#include <windows.h>
#endif

Gui::Gui(PlotHandler* plotHandler, VariableHandler* variableHandler, ConfigHandler* configHandler, PlotGroupHandler* plotGroupHandler, IFileHandler* fileHandler, PlotHandler* tracePlotHandler, ViewerDataHandler* viewerDataHandler, TraceDataHandler* traceDataHandler, std::atomic<bool>& done, std::mutex* mtx, spdlog::logger* logger, std::string& projectPath) : plotHandler(plotHandler), variableHandler(variableHandler), configHandler(configHandler), plotGroupHandler(plotGroupHandler), fileHandler(fileHandler), tracePlotHandler(tracePlotHandler), viewerDataHandler(viewerDataHandler), traceDataHandler(traceDataHandler), done(done), mtx(mtx), logger(logger)
{
	threadHandle = std::thread(&Gui::mainThread, this, projectPath);
	plotEditWindow = std::make_shared<PlotEditWindow>(plotHandler, plotGroupHandler, variableHandler);
	plotsTree = std::make_shared<PlotsTree>(viewerDataHandler, plotHandler, plotGroupHandler, variableHandler, plotEditWindow, fileHandler, logger);
	variableTable = std::make_shared<VariableTableWindow>(viewerDataHandler, plotHandler, variableHandler, &projectElfPath, &projectConfigPath, logger);

	variableHandler->renameCallback = [&](std::string oldName, std::string newName)
	{
		for (std::shared_ptr<Plot> plt : *this->plotHandler)
			plt->renameSeries(oldName, newName);
	};
}

Gui::~Gui()
{
	if (threadHandle.joinable())
		threadHandle.join();
}

static void glfw_error_callback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static float getContentScale(GLFWwindow* window)
{
	float xscale;
	float yscale;
	glfwGetWindowContentScale(window, &xscale, &yscale);
	return (xscale + yscale) / 2.0f;
}

void Gui::mainThread(std::string externalPath)
{
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return;

	GLFWwindow* window = glfwCreateWindow(1500, 1000, (std::string("MCUViewer - Special Edition  ") + projectConfigPath).c_str(), NULL, NULL);
	if (window == NULL)
		return;
	glfwMakeContextCurrent(window);
	glfwMaximizeWindow(window);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();

	GuiHelper::contentScale = getContentScale(window);

	ImFontConfig cfg;
	cfg.SizePixels = 13.0f * GuiHelper::contentScale;

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

#ifdef USE_EMBEDDED_FONT
	extern unsigned char font_ttf[];
	extern unsigned int font_ttf_len;
	io.Fonts->AddFontFromMemoryTTF((void*)font_ttf, font_ttf_len, 15.0f * GuiHelper::contentScale);
#else
	// Load custom font
	if (access("font.ttf", F_OK) == 0)
		io.Fonts->AddFontFromFileTTF("font.ttf", 15.0f * GuiHelper::contentScale);
	else
		io.Fonts->AddFontDefault(&cfg);
#endif
	
	io.FontGlobalScale = 1.0f;
	ImGui::GetPlatformIO().Platform_LocaleDecimalPoint = *localeconv()->decimal_point;

	ImGui::StyleColorsDark();
	ImPlot::StyleColorsDark();

	ImGui::GetStyle().ScaleAllSizes(GuiHelper::contentScale);
	ImGui::GetStyle().Colors[ImGuiCol_PopupBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 130");

	ImGuiWindowClass window_class;
	window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;

	fileHandler->init();

	jlinkProbe = std::make_shared<JlinkDebugProbe>(logger);
	stlinkProbe = std::make_shared<StlinkDebugProbe>(logger);
	debugProbeDevice = stlinkProbe;
	viewerDataHandler->setDebugProbe(debugProbeDevice);

	jlinkTraceProbe = std::make_shared<JlinkTraceProbe>(logger);
	stlinkTraceProbe = std::make_shared<StlinkTraceProbe>(logger);
	traceProbeDevice = stlinkTraceProbe;
	traceDataHandler->setDebugProbe(traceProbeDevice);

	if (!externalPath.empty())
		openProject(externalPath);
	else
	{
		// Check if we should prompt to reopen last project
		std::string lastPath = getLastProjectPath();
		if (!lastPath.empty())
			showReopenProjectPrompt = true;
	}

	while (!done)
	{
		if (glfwGetWindowAttrib(window, GLFW_ICONIFIED))
		{
			glfwWaitEvents();
			continue;
		}

		if (glfwGetWindowAttrib(window, GLFW_FOCUSED) || (traceDataHandler->getState() == DataHandlerBase::State::RUN) || (viewerDataHandler->getState() == DataHandlerBase::State::RUN))
			glfwSwapInterval(1);
		else
			glfwSwapInterval(4);

		glfwSetWindowTitle(window, (std::string("MCUViewer - Special Edition  ") + projectConfigPath).c_str());
		glfwPollEvents();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();

		ImGui::NewFrame();
		ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);

		if (showDemoWindow)
			ImPlot::ShowDemoWindow();

		if (glfwWindowShouldClose(window))
		{
			viewerDataHandler->setState(DataHandlerBase::State::STOP);
			traceDataHandler->setState(DataHandlerBase::State::STOP);

			if (configHandler->isSavingRequired(projectElfPath))
				askShouldSaveOnExit(glfwWindowShouldClose(window));
			else
				done = true;
		}
		glfwSetWindowShouldClose(window, done);
		checkShortcuts();

		drawMenu();
		drawAboutWindow();
		drawPreferencesWindow();
		promptReopenLastProject();

		if (ImGui::Begin("Trace Viewer"))
		{
			activeView = ActiveViewType::TraceViewer;
			drawAcqusitionSettingsWindow(activeView);
			ImGui::SetNextWindowClass(&window_class);
			if (ImGui::Begin("Trace Plots"))
				drawPlotsSwo();
			ImGui::End();
			drawStartButton(traceDataHandler);
			drawSettingsSwo();
			drawIndicatorsSwo();
			drawPlotsTreeSwo();
		}
		ImGui::End();

		if (ImGui::Begin("Var Viewer"))
		{
			activeView = ActiveViewType::VarViewer;
			drawAcqusitionSettingsWindow(activeView);
			drawStartButton(viewerDataHandler);
			
			// Calculate available space for sidebar content
			float windowHeight = ImGui::GetWindowSize().y;
			float currentY = ImGui::GetCursorPosY();
			float availableHeight = windowHeight - currentY - 20; // 20px padding
			
			// Ensure plots tree gets minimum 30% of sidebar height
			float minPlotsTreeHeight = availableHeight * 0.3f;
			static float plotsTreeHeight = std::max(200.0f * GuiHelper::contentScale, minPlotsTreeHeight);
			plotsTreeHeight = std::max(plotsTreeHeight, minPlotsTreeHeight);
			
			// Calculate variable table height
			float varTableHeight = availableHeight - plotsTreeHeight - 8; // 8px for splitter
			varTableHeight = std::max(varTableHeight, 150.0f * GuiHelper::contentScale); // Minimum var table height
			
			// Adjust plots tree height if variable table needs more space
			if (varTableHeight < 150.0f * GuiHelper::contentScale) {
				varTableHeight = 150.0f * GuiHelper::contentScale;
				plotsTreeHeight = availableHeight - varTableHeight - 8;
			}
			
			// Draw variable table in top section of sidebar
			variableTable->drawWithHeight(varTableHeight);
			
			// Draw draggable splitter between variables and plots tree
			ImGui::Button("##sidebar_splitter", ImVec2(-1, 8));
			if (ImGui::IsItemActive()) {
				float delta = ImGui::GetIO().MouseDelta.y;
				plotsTreeHeight -= delta; // Decrease plots tree height when dragging up
				// Enforce constraints
				plotsTreeHeight = std::max(plotsTreeHeight, minPlotsTreeHeight);
				plotsTreeHeight = std::min(plotsTreeHeight, availableHeight - 150.0f * GuiHelper::contentScale - 8);
			}
			ImGui::SetItemTooltip("Drag to resize");
			
			// Draw plots tree in bottom section of sidebar with calculated height
			ImGui::BeginChild("PlotsTreeSection", ImVec2(-1, plotsTreeHeight), false, ImGuiWindowFlags_None);
			plotsTree->draw();
			ImGui::EndChild();
			
			plotEditWindow->draw();
			
			// Draw main plots window (separate from sidebar)
			ImGui::SetNextWindowClass(&window_class);
			if (ImGui::Begin("Plots"))
				drawPlots();
			ImGui::End();
		}
		ImGui::End();

		popup.handle();

		// Rendering
		ImGui::Render();
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}

		glfwSwapBuffers(window);
	}

	logger->info("Exiting GUI main thread");

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
	fileHandler->deinit();
}

void Gui::drawMenu()
{
	bool shouldSaveOnClose = false;
	bool shouldSaveOnNew = false;
	ImGui::BeginMainMenuBar();

	bool active = !(viewerDataHandler->getState() == DataHandlerBase::State::RUN || traceDataHandler->getState() == DataHandlerBase::State::RUN);

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New", NULL, nullptr, active))
			shouldSaveOnNew = true;

		if (ImGui::MenuItem("Open", "Ctrl+O", nullptr, active))
			openProject();

		if (ImGui::MenuItem("Save", "Ctrl+S", false, (!projectConfigPath.empty())))
			saveProject();

		if (ImGui::MenuItem("Save As"))
			saveProjectAs();

		if (ImGui::MenuItem("Quit", NULL, nullptr, active))
			shouldSaveOnClose = true;

		ImGui::EndMenu();
	}

	ImGui::MenuItem("Acquisition setting", NULL, &showAcqusitionSettingsWindow, active);
	ImGui::MenuItem("Preferences", NULL, &showPreferencesWindow, active);
	ImGui::MenuItem("About", NULL, &showAboutWindow, active);

	if (activeView == ActiveViewType::VarViewer)
	{
		ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 210 * GuiHelper::contentScale));
		GuiHelper::drawDescriptionWithNumber("sampling: ", viewerDataHandler->getAverageSamplingFrequency(), " Hz", 2);
	}

	ImGui::EndMainMenuBar();
	askShouldSaveOnExit(shouldSaveOnClose);
	askShouldSaveOnNew(shouldSaveOnNew);
}

void Gui::drawStartButton(DataHandlerBase* activeDataHandler)
{
	bool shouldDisableButton = (!devicesList.empty() && devicesList.front() == noDevices);
	ImGui::BeginDisabled(shouldDisableButton);

	DataHandlerBase::State state = activeDataHandler->getState();

	if (state == DataHandlerBase::State::RUN)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, GuiHelper::green);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GuiHelper::greenLight);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, GuiHelper::greenLightDim);
	}
	else if (state == DataHandlerBase::State::STOP)
	{
		if (activeDataHandler->getLastReaderError() != "")
		{
			ImGui::PushStyleColor(ImGuiCol_Button, GuiHelper::red);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GuiHelper::redLight);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, GuiHelper::redLightDim);
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, GuiHelper::orange);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GuiHelper::orangeLight);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, GuiHelper::orangeLightDim);
		}
	}

	if ((ImGui::Button((viewerStateMap.at(state) + " " + activeDataHandler->getLastReaderError()).c_str(), ImVec2(-1, 50 * GuiHelper::contentScale)) ||
		 (ImGui::IsKeyPressed(ImGuiKey_Space, false) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))) &&
		!shouldDisableButton)
	{
		if (state == DataHandlerBase::State::STOP)
		{
			logger->info("Start clicked!");
			plotHandler->eraseAllPlotData();
			tracePlotHandler->eraseAllPlotData();
			activeDataHandler->setState(DataHandlerBase::State::RUN);
		}
		else
		{
			logger->info("Stop clicked!");
			activeDataHandler->setState(DataHandlerBase::State::STOP);
		}
	}

	ImGui::PopStyleColor(3);
	ImGui::EndDisabled();
}

void Gui::drawAcqusitionSettingsWindow(ActiveViewType type)
{
	if (showAcqusitionSettingsWindow)
		ImGui::OpenPopup("Acqusition Settings");

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(950 * GuiHelper::contentScale, 600 * GuiHelper::contentScale));
	if (ImGui::BeginPopupModal("Acqusition Settings", &showAcqusitionSettingsWindow, 0))
	{
		if (type == ActiveViewType::VarViewer)
			acqusitionSettingsViewer();
		else if (type == ActiveViewType::TraceViewer)
			acqusitionSettingsTrace();

		acqusitionErrorPopup.handle();

		const float buttonHeight = 25.0f * GuiHelper::contentScale;
		ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowSize().y - buttonHeight / 2.0f - ImGui::GetFrameHeightWithSpacing()));

		if (ImGui::Button("Done", ImVec2(-1, buttonHeight)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			showAcqusitionSettingsWindow = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void Gui::drawPreferencesWindow()
{
	if (showPreferencesWindow)
		ImGui::OpenPopup("Preferences");

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(500 * GuiHelper::contentScale, 250 * GuiHelper::contentScale));
	if (ImGui::BeginPopupModal("Preferences", &showPreferencesWindow, 0))
	{
		ImGuiIO& io = ImGui::GetIO();

		ImGui::DragFloat("font size", &io.FontGlobalScale, 0.005f, 0.8f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

		const float buttonHeight = 25.0f * GuiHelper::contentScale;
		ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowSize().y - buttonHeight / 2.0f - ImGui::GetFrameHeightWithSpacing()));
		if (ImGui::Button("Done", ImVec2(-1, buttonHeight)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			showPreferencesWindow = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void Gui::askShouldSaveOnExit(bool shouldOpenPopup)
{
	if (shouldOpenPopup)
		ImGui::OpenPopup("Save?");

	auto onYes = [&]()
	{
		done = true;
		if (!saveProject())
			saveProjectAs();
	};

	auto onNo = [&]()
	{ done = true; };
	auto onCancel = [&]()
	{ done = false; };

	if (variableHandler->isEmpty() && projectElfPath.empty() && shouldOpenPopup)
		done = true;

	GuiHelper::showQuestionBox("Save?", "Do you want to save the current config?\n", onYes, onNo, onCancel);
}

void Gui::askShouldSaveOnNew(bool shouldOpenPopup)
{
	auto onNo = [&]()
	{
		variableHandler->clear();
		plotHandler->removeAllPlots();
		traceDataHandler->initPlots();
		plotGroupHandler->removeAllGroups();
		projectElfPath = "";
		projectConfigPath = "";
	};

	if (variableHandler->isEmpty() && projectElfPath.empty() && shouldOpenPopup)
		onNo();
	else if (shouldOpenPopup)
		ImGui::OpenPopup("SaveOnNew?");

	auto onYes = [&]()
	{
		if (!saveProject())
			saveProjectAs();
		onNo();
	};

	GuiHelper::showQuestionBox("SaveOnNew?", "Do you want to save the current config?\n", onYes, onNo, []() {});
}

bool Gui::saveProject()
{
	if (!projectConfigPath.empty())
		return configHandler->saveConfigFile(projectElfPath, "");
	return false;
}

bool Gui::saveProjectAs()
{
	std::string path = fileHandler->saveFile(std::pair<std::string, std::string>("Project files", "cfg"));
	if (path != "")
	{
		projectConfigPath = path;
		configHandler->saveConfigFile(projectElfPath, projectConfigPath);
		logger->info("Project config path: {}", projectConfigPath);
		return true;
	}
	return false;
}

bool Gui::openProject(std::string externalPath)
{
	std::string path = "";

	if (externalPath.empty())
		path = fileHandler->openFile(std::pair<std::string, std::string>("Project files", "cfg"));
	else
		path = externalPath;

	if (path != "")
	{
		projectConfigPath = path;
		configHandler->changeConfigFile(projectConfigPath);
		variableHandler->clear();
		plotHandler->removeAllPlots();
		configHandler->readConfigFile(projectElfPath);

		logger->info("Project config path: {}", projectConfigPath);
		/* TODO refactor */
		devicesList.clear();
		if (viewerDataHandler->getProbeSettings().debugProbe == 1)
			debugProbeDevice = jlinkProbe;
		else
			debugProbeDevice = stlinkProbe;

		viewerDataHandler->setDebugProbe(debugProbeDevice);

		if (traceDataHandler->getProbeSettings().debugProbe == 1)
			traceProbeDevice = jlinkTraceProbe;
		else
			traceProbeDevice = stlinkTraceProbe;

		traceDataHandler->setDebugProbe(traceProbeDevice);

		// Cache the successfully opened project path
		saveLastProjectPath(path);
		
		return true;
	}
	return false;
}

std::string Gui::getLastProjectPath()
{
	std::string cacheDir = ".cache";
	std::string cacheFile = cacheDir + "/last_project.cache";
	std::ifstream file(cacheFile);
	if (file.is_open())
	{
		std::string path;
		std::getline(file, path);
		file.close();
		// Check if the cached file still exists
		if (!path.empty() && std::filesystem::exists(path))
			return path;
	}
	return "";
}

void Gui::saveLastProjectPath(const std::string& path)
{
	if (!path.empty())
	{
		std::string cacheDir = ".cache";
		std::string cacheFile = cacheDir + "/last_project.cache";

		// Create cache directory if it doesn't exist
		std::filesystem::create_directories(cacheDir);

		std::ofstream file(cacheFile);
		if (file.is_open())
		{
			file << path << std::endl;
			file.close();
		}
	}
}

void Gui::promptReopenLastProject()
{
	if (showReopenProjectPrompt)
		ImGui::OpenPopup("Reopen Last Project?");

	if (ImGui::BeginPopupModal("Reopen Last Project?", &showReopenProjectPrompt, ImGuiWindowFlags_AlwaysAutoResize))
	{
		std::string lastPath = getLastProjectPath();
		std::string filename = std::filesystem::path(lastPath).filename().string();
		
		ImGui::Text("Reopen last project: %s?", filename.c_str());
		
		if (ImGui::Button("Yes"))
		{
			openProject(lastPath);
			showReopenProjectPrompt = false;
			ImGui::CloseCurrentPopup();
		}
		
		ImGui::SameLine();
		if (ImGui::Button("No"))
		{
			showReopenProjectPrompt = false;
			ImGui::CloseCurrentPopup();
		}
		
		ImGui::EndPopup();
	}
}

bool Gui::openElfFile()
{
	std::string path = fileHandler->openFile({"Elf files", "elf"});

	if (path.find(" ") != std::string::npos)
	{
		acqusitionErrorPopup.show("Error!", "Selected path contains spaces!", 2.0f);
		projectElfPath = "";
		return false;
	}

	if (path != "")
	{
		std::filesystem::path relPath = std::filesystem::relative(path, std::filesystem::path(projectConfigPath).parent_path());
		if (relPath != "")
			projectElfPath = relPath.string();
		else
			projectElfPath = path;

		logger->info("Project elf file path: {}", projectElfPath);
		return true;
	}
	return false;
}

bool Gui::openLogDirectory(std::string& logDirectory)
{
	std::string path = fileHandler->openDirectory({"", ""});

	if (path != "")
	{
		logDirectory = path;
		logger->info("Log directory: {}", path);
		return true;
	}
	return false;
}

void Gui::checkShortcuts()
{
	const ImGuiIO& io = ImGui::GetIO();
	bool wasSaved = false;

	if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
		openProject();
	else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
	{
		wasSaved = saveProject();
		if (!wasSaved)
			saveProjectAs();

		popup.show("Info", "Saving successful!", 0.65f);
	}
}

void Gui::showChangeFormatPopup(const char* text, Plot& plt, const std::string& name)
{
	int format = static_cast<int>(plt.getSeriesDisplayFormat(name));

	if (plt.getSeries(name)->var->getType() == Variable::Type::F32)
		return;

	if (ImGui::BeginPopupContextItem(name.c_str()))
	{
		if (ImGui::RadioButton("dec", &format, 0))
			ImGui::CloseCurrentPopup();
		if (ImGui::RadioButton("hex", &format, 1))
			ImGui::CloseCurrentPopup();
		if (ImGui::RadioButton("bin", &format, 2))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	plt.setSeriesDisplayFormat(name, static_cast<Plot::displayFormat>(format));
}
