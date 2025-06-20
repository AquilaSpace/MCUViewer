#ifndef _GUI_PLOTEDIT_HPP
#define _GUI_PLOTEDIT_HPP

#include "GuiHelper.hpp"
#include "GuiSelectVariable.hpp"
#include "Plot.hpp"
#include "PlotGroupHandler.hpp"
#include "PlotHandler.hpp"
#include "Popup.hpp"
#include "imgui.h"

class PlotEditWindow
{
   public:
	PlotEditWindow(PlotHandler* plotHandler, PlotGroupHandler* plotGroupHandler, VariableHandler* variableHandler) : plotHandler(plotHandler), plotGroupHandler(plotGroupHandler), variableHandler(variableHandler)
	{
		selectVariableWindow = std::make_unique<SelectVariableWindow>(variableHandler, &selection, 1);
	}

	void draw()
	{
		if (showPlotEditWindow)
			ImGui::OpenPopup("Plot Edit");

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		
		// Calculate dynamic window size based on content
		float baseHeight = 500 * GuiHelper::contentScale; // Base height for basic controls
		float extraHeight = 0;
		
		if (editedPlot && editedPlot->getType() == Plot::Type::XY)
		{
			// Add height for each series in XY plots (for series-specific X-axis config)
			auto& seriesMap = editedPlot->getSeriesMap();
			extraHeight = seriesMap.size() * 35 * GuiHelper::contentScale; // ~35px per series
			extraHeight = std::min(extraHeight, 300.0f * GuiHelper::contentScale); // Cap at 300px extra
		}
		
		float totalHeight = baseHeight + extraHeight;
		float windowWidth = 800 * GuiHelper::contentScale;
		
		ImGui::SetNextWindowSize(ImVec2(windowWidth, totalHeight));
		if (ImGui::BeginPopupModal("Plot Edit", &showPlotEditWindow, 0))
		{
			drawPlotEditSettings();

			const float buttonHeight = 25.0f * GuiHelper::contentScale;
			ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowSize().y - buttonHeight / 2.0f - ImGui::GetFrameHeightWithSpacing()));

			if (ImGui::Button("Done", ImVec2(-1, buttonHeight)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				showPlotEditWindow = false;
				ImGui::CloseCurrentPopup();
			}

			popup.handle();
			selectVariableWindow->draw();
			ImGui::EndPopup();
		}
	}

	void setPlotToEdit(std::shared_ptr<Plot> plot)
	{
		editedPlot = plot;
	}

	void setShowPlotEditWindowState(bool state)
	{
		if (showPlotEditWindow != state)
			stateChanged = true;
		showPlotEditWindow = state;
	}

	void drawPlotEditSettings()
	{
		if (editedPlot == nullptr)
			return;

		std::string name = editedPlot->getName();

		ImGui::Dummy(ImVec2(-1, 5));
		GuiHelper::drawCenteredText("Plot");
		ImGui::Separator();

		GuiHelper::drawTextAlignedToSize("name:", alignment);
		ImGui::SameLine();

		if (stateChanged)
		{
			ImGui::SetKeyboardFocusHere(0);
			stateChanged = false;
		}

		ImGui::InputText("##name", &name, ImGuiInputTextFlags_None, NULL, NULL);

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			if (!plotHandler->checkIfPlotExists(name))
			{
				std::string oldName = editedPlot->getName();
				plotHandler->renamePlot(oldName, name);
				plotGroupHandler->renamePlotInAllGroups(oldName, name);
			}
			else
				popup.show("Error!", "Plot already exists!", 1.5f);
		}

		const char* plotTypes[] = {"curve", "bar", "table", "XY"};
		int32_t typeCombo = (int32_t)editedPlot->getType();
		GuiHelper::drawTextAlignedToSize("type:", alignment);
		ImGui::SameLine();
		if (ImGui::Combo("##combo", &typeCombo, plotTypes, IM_ARRAYSIZE(plotTypes)))
			editedPlot->setType((Plot::Type)typeCombo);

		if (editedPlot->getType() == Plot::Type::XY)
		{
			GuiHelper::drawTextAlignedToSize("Default X-axis:", alignment);
			ImGui::SameLine();

			std::string selectedVariable = "";

			if (selection.empty())
				selectedVariable = editedPlot->getXAxisVariable() ? editedPlot->getXAxisVariable()->getName() : "";
			else
				selectedVariable = *selection.begin();

			ImGui::InputText("##defaultX", &selectedVariable, 0, NULL, NULL);
			if (variableHandler->contains(selectedVariable))
				editedPlot->setXAxisVariable(variableHandler->getVariable(selectedVariable).get());
			ImGui::SameLine();
			if (ImGui::Button("select...##defaultX", ImVec2(65 * GuiHelper::contentScale, 19 * GuiHelper::contentScale)))
				selectVariableWindow->setShowState(true);

			// Series-specific X-axis configuration
			ImGui::Separator();
			GuiHelper::drawCenteredText("Per-Series X-Axis Variables");
			ImGui::Separator();

			drawSeriesXAxisConfiguration();
		}

		// Add axis limits controls
		ImGui::Separator();
		GuiHelper::drawCenteredText("Axis Limits");
		ImGui::Separator();

		// X-axis limits: only allow for XY plots (other plots use time)
		bool enableXAxisLimits = (editedPlot->getType() == Plot::Type::XY);
		drawAxisLimits("X", editedPlot->xAxisLimits, enableXAxisLimits);

		// Y-axis limits: allow for all plots except TABLE (which doesn't draw plots)
		bool enableYAxisLimits = (editedPlot->getType() != Plot::Type::TABLE);
		drawAxisLimits("Y", editedPlot->yAxisLimits, enableYAxisLimits);
	}

   private:
	void drawSeriesXAxisConfiguration()
	{
		if (!editedPlot || editedPlot->getType() != Plot::Type::XY)
			return;

		auto& seriesMap = editedPlot->getSeriesMap();
		if (seriesMap.empty())
		{
			ImGui::Text("No series added to this plot");
			return;
		}

		ImGui::Text("Configure X-axis variable for each Y-series:");
		ImGui::Text("(Leave empty to use default X-axis)");
		ImGui::Spacing();

		for (auto& [seriesName, series] : seriesMap)
		{
			ImGui::PushID(seriesName.c_str());

			// Series name with color indicator
			Variable::Color color = series->var->getColor();
			ImVec4 col = {color.r, color.g, color.b, color.a};
			ImGui::ColorButton("##seriesColor", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip, ImVec2(10 * GuiHelper::contentScale, 10 * GuiHelper::contentScale));
			ImGui::SameLine();

			// Series name and X-axis variable selector
			std::string labelText = seriesName + " X-axis:";
			GuiHelper::drawTextAlignedToSize(std::move(labelText), alignment);
			ImGui::SameLine();

			// Get current X-axis variable for this series
			std::string currentXVar = "";
			Variable* currentXAxisVar = editedPlot->getSeriesXAxisVariable(seriesName);
			if (currentXAxisVar)
				currentXVar = currentXAxisVar->getName();

			// Input text field for X-axis variable (like the default X-axis selector)
			ImGui::InputText("##seriesXAxis", &currentXVar, 0, NULL, NULL);
			if (currentXVar.empty())
			{
				// Empty means use default X-axis
				editedPlot->setSeriesXAxisVariable(seriesName, nullptr);
			}
			else if (variableHandler->contains(currentXVar))
			{
				// Valid variable name - set as series X-axis
				editedPlot->setSeriesXAxisVariable(seriesName, variableHandler->getVariable(currentXVar).get());
			}

			ImGui::SameLine();
			if (ImGui::Button("select...", ImVec2(65 * GuiHelper::contentScale, 19 * GuiHelper::contentScale)))
			{
				// TODO: Create a series-specific variable selector or reuse the existing one
				// For now, user can type the variable name directly
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear", ImVec2(50 * GuiHelper::contentScale, 19 * GuiHelper::contentScale)))
			{
				editedPlot->setSeriesXAxisVariable(seriesName, nullptr);
			}

			ImGui::PopID();
		}
	}

	void drawAxisLimits(const char* axis, Plot::AxisLimits& limits, bool enabled = true)
	{
		// Disable the entire axis section if not enabled for this plot type
		ImGui::BeginDisabled(!enabled);

		GuiHelper::drawTextAlignedToSize(std::string(axis) + "-axis:", alignment);
		ImGui::SameLine();
		ImGui::Text("auto-fit:");
		ImGui::SameLine();
		ImGui::Checkbox(std::string("##" + std::string(axis) + "_auto").c_str(), &limits.autoFit);
		ImGui::SameLine();
		ImGui::Text("min:");
		ImGui::SameLine();
		ImGui::BeginDisabled(limits.autoFit);
		ImGui::SetNextItemWidth(80 * GuiHelper::contentScale);
		if (ImGui::InputDouble(std::string("##" + std::string(axis) + "_min").c_str(), &limits.min, 0, 0, "%.3f"))
		{
			if (limits.min >= limits.max)
			{
				limits.max = limits.min + 1.0;	// Auto-adjust max to be greater than min
			}
		}
		ImGui::SameLine();
		ImGui::Text("max:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80 * GuiHelper::contentScale);
		if (ImGui::InputDouble(std::string("##" + std::string(axis) + "_max").c_str(), &limits.max, 0, 0, "%.3f"))
		{
			if (limits.max <= limits.min)
			{
				limits.min = limits.max - 1.0;	// Auto-adjust min to be less than max
			}
		}
		ImGui::EndDisabled();

		ImGui::EndDisabled();
	}

	/**
	 * @brief Text alignemnt in front of the input fields
	 *
	 */
	static constexpr size_t alignment = 18;

	bool showPlotEditWindow = false;
	bool stateChanged = false;

	std::shared_ptr<Plot> editedPlot = nullptr;

	PlotHandler* plotHandler;
	PlotGroupHandler* plotGroupHandler;
	VariableHandler* variableHandler;

	Popup popup;

	std::set<std::string> selection;
	std::unique_ptr<SelectVariableWindow> selectVariableWindow;
};

#endif