#pragma once

#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "GdbParser.hpp"
#include "GuiHelper.hpp"
#include "GuiImportVariables.hpp"
#include "GuiVariablesEdit.hpp"
#include "PlotHandler.hpp"
#include "Popup.hpp"
#include "Variable.hpp"
#include "VariableHandler.hpp"
#include "ViewerDataHandler.hpp"

class VariableTableWindow
{
   public:
	VariableTableWindow(ViewerDataHandler* viewerDataHandler, PlotHandler* plotHandler, VariableHandler* variableHandler, std::string* projectElfPath, std::string* projectConfigPath, spdlog::logger* logger) : viewerDataHandler(viewerDataHandler), plotHandler(plotHandler), variableHandler(variableHandler), projectElfPath(projectElfPath), projectConfigPath(projectConfigPath), logger(logger)
	{
		parser = std::make_shared<GdbParser>(variableHandler, logger);
		variableEditWindow = std::make_shared<VariableEditWindow>(variableHandler);
		importVariablesWindow = std::make_shared<ImportVariablesWindow>(parser.get(), projectElfPath, projectConfigPath, variableHandler);
	}

	void draw()
	{
		drawWithHeight(300 * GuiHelper::contentScale); // Default height
	}

	void drawWithHeight(float tableHeight)
	{
		ImGui::BeginDisabled(viewerDataHandler->getState() == DataHandlerBase::State::RUN);
		ImGui::Dummy(ImVec2(-1, 5));
		GuiHelper::drawCenteredText("Variables");
		ImGui::SameLine();
		ImGui::HelpMarker("Select your *.elf file in the Options->Acqusition Settings to import or update the variables.");
		ImGui::Separator();

		drawAddVariableButton();
		drawUpdateAddressesFromElf();

		const char* label = "search ";
		ImGui::PushItemWidth(ImGui::GetItemRectSize().x - ImGui::CalcTextSize(label).x - 8 * GuiHelper::contentScale);
		static std::string search{};
		ImGui::Text("%s", label);
		ImGui::SameLine();
		ImGui::InputText("##search", &search, 0, NULL, NULL);
		ImGui::PopItemWidth();

		// View toggle buttons
		ImGui::Spacing();
		static bool treeView = true;
		if (ImGui::RadioButton("Tree View", treeView))
		{
			treeView = true;
			rebuildTree = true;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Flat View", !treeView))
		{
			treeView = false;
		}

		if (treeView)
		{
			// Tree view controls
			ImGui::SameLine();
			ImGui::Spacing();
			ImGui::SameLine();
			if (ImGui::Button("Expand All"))
			{
				expandAll = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Collapse All"))
			{
				collapseAll = true;
			}

			drawTreeView(search, tableHeight);
		}
		else
		{
			drawFlatView(search, tableHeight);
		}

		ImGui::EndDisabled();

		variableEditWindow->draw();
		importVariablesWindow->draw();
	}

private:
	struct TreeNode
	{
		std::map<std::string, std::shared_ptr<TreeNode>> children;
		std::vector<std::shared_ptr<Variable>> variables;
	};

	void drawFlatView(const std::string& search, float tableHeight)
	{
		static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable;

		if (ImGui::BeginTable("table_scrolly", 3, flags, ImVec2(0.0f, tableHeight)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100 * GuiHelper::contentScale);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80 * GuiHelper::contentScale);
			ImGui::TableHeadersRow();

			std::optional<std::string> varNameToDelete;
			std::string currentName{};

			for (std::shared_ptr<Variable> var : *variableHandler)
			{
				std::string name = var->getName();
				if (toLower(name).find(toLower(search)) == std::string::npos)
					continue;

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushID(name.c_str());
				ImGui::ColorEdit4("##", &var->getColor().r, ImGuiColorEditFlags_NoInputs);
				ImGui::SameLine();
				ImGui::PopID();

				const bool itemIsSelected = selection.contains(name);

				if (ImGui::Selectable(var->getName().c_str(), itemIsSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap | ImGuiSelectableFlags_AllowDoubleClick))
				{
					if (ImGui::IsMouseDoubleClicked(0))
					{
						variableEditWindow->setVariableToEdit(var);
						variableEditWindow->setShowVariableEditWindowState(true);
					}

					if (ImGui::GetIO().KeyCtrl && var->getIsFound())
					{
						if (itemIsSelected)
							selection.erase(name);
						else
							selection.insert(name);
					}
					else
						selection.clear();
				}

				drawMenuVariablePopup(name, [&]()
									  { variableHandler->addNewVariable(""); }, [&](std::string name)
									  { variableHandler->addNewVariable(name); }, [&](std::string name)
									  { varNameToDelete = name; }, [&](std::string name)
									  {	variableEditWindow->setVariableToEdit(var);
                                                                    variableEditWindow->setShowVariableEditWindowState(true); });

				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
				{
					if (selection.empty())
						selection.insert(name);

					/* pass a pointer to the selection as we have to call clear() on the original object upon receiving */
					std::set<std::string>* selectionPtr = &selection;
					ImGui::SetDragDropPayload("MY_DND", &selectionPtr, sizeof(selectionPtr));
					ImGui::PushID(name.c_str());
					ImGui::ColorEdit4("##", &variableHandler->getVariable(*selection.begin())->getColor().r, ImGuiColorEditFlags_NoInputs);
					ImGui::SameLine();
					ImGui::PopID();

					if (selection.size() > 1)
						ImGui::TextUnformatted("<multiple vars>");
					else
						ImGui::TextUnformatted(selection.begin()->c_str());
					ImGui::EndDragDropSource();
				}
				ImGui::TableSetColumnIndex(1);

				if (var->getIsFound())
					ImGui::Text("%s", ("0x" + std::string(GuiHelper::intToHexString(var->getAddress()))).c_str());
				else
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "NOT FOUND!");

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%s", var->getTypeStr().c_str());
			}

			if (varNameToDelete.has_value())
			{
				for (std::shared_ptr<Plot> plt : *plotHandler)
					plt->removeSeries(varNameToDelete.value_or(""));
				variableHandler->erase(varNameToDelete.value_or(""));
			}
			ImGui::EndTable();
		}
	}

	void drawTreeView(const std::string& search, float tableHeight)
	{
		// Build hierarchical tree for variables
		static std::shared_ptr<TreeNode> rootNode = nullptr;
		static std::string lastSearchString = "";
		static size_t lastVariableCount = 0;
		static bool treeBuilt = false;

		// Get current variable count to detect changes
		size_t currentVariableCount = 0;
		for (auto var : *variableHandler) { currentVariableCount++; }

		// Rebuild tree if search changed, variable list changed, or first time
		if (lastSearchString != search || !rootNode || rebuildTree || !treeBuilt || 
			lastVariableCount != currentVariableCount)
		{
			rootNode = buildVariableTree(search);
			lastSearchString = search;
			lastVariableCount = currentVariableCount;
			rebuildTree = false;
			treeBuilt = true;
		}

		static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable;

		if (ImGui::BeginTable("variable_tree", 3, flags, ImVec2(0.0f, tableHeight)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 100 * GuiHelper::contentScale);
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80 * GuiHelper::contentScale);
			ImGui::TableHeadersRow();

			std::optional<std::string> varNameToDelete;

			// Draw tree
			if (rootNode)
			{
				drawVariableTreeNode("Root", rootNode, 0, varNameToDelete);
			}

			if (varNameToDelete.has_value())
			{
				for (std::shared_ptr<Plot> plt : *plotHandler)
					plt->removeSeries(varNameToDelete.value_or(""));
				variableHandler->erase(varNameToDelete.value_or(""));
			}

			ImGui::EndTable();
		}

		// Reset expand/collapse flags
		expandAll = false;
		collapseAll = false;
	}

	std::shared_ptr<TreeNode> buildVariableTree(const std::string& searchFilter)
	{
		auto root = std::make_shared<TreeNode>();

		for (std::shared_ptr<Variable> var : *variableHandler)
		{
			std::string name = var->getName();
			
			// Apply search filter
			if (!searchFilter.empty() && toLower(name).find(toLower(searchFilter)) == std::string::npos)
				continue;

			// Parse namespace hierarchy (split by :: or .)
			std::vector<std::string> parts;
			std::string current = name;
			
			// Replace :: with . for consistent parsing
			size_t pos = 0;
			while ((pos = current.find("::", pos)) != std::string::npos)
			{
				current.replace(pos, 2, ".");
				pos += 1;
			}
			
			// Split by . to get hierarchy levels
			std::istringstream iss(current);
			std::string part;
			while (std::getline(iss, part, '.'))
			{
				if (!part.empty())
					parts.push_back(part);
			}

			// Navigate/create tree structure
			auto currentNode = root;
			for (size_t i = 0; i < parts.size() - 1; ++i)
			{
				const std::string& group = parts[i];
				if (currentNode->children.find(group) == currentNode->children.end())
				{
					currentNode->children[group] = std::make_shared<TreeNode>();
				}
				currentNode = currentNode->children[group];
			}

			// Add variable to the appropriate node
			currentNode->variables.push_back(var);
		}

		return root;
	}

	void drawVariableTreeNode(const std::string& nodeName, std::shared_ptr<TreeNode> node, int depth, std::optional<std::string>& varNameToDelete)
	{
		if (!node) return;

		// Draw child groups first
		for (auto& [groupName, childNode] : node->children)
		{
			if (!childNode) continue;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImGui::Indent(depth * ImGui::GetStyle().IndentSpacing);

			// Group header with collapsible tree
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
			
			// Use a unique ID for this tree node to maintain state
			std::string nodeId = std::string("##tree_") + groupName + "_" + std::to_string(depth);
			
			// Handle expand/collapse all
			if (expandAll)
			{
				ImGui::SetNextItemOpen(true);
			}
			else if (collapseAll)
			{
				ImGui::SetNextItemOpen(false);
			}
			else if (depth == 0)
			{
				// Top-level nodes open by default
				flags |= ImGuiTreeNodeFlags_DefaultOpen;
			}

			bool nodeOpen = ImGui::TreeNodeEx((groupName + nodeId).c_str(), flags, "%s", groupName.c_str());
			
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(""); // Empty address column for groups
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(""); // Empty type column for groups

			if (nodeOpen)
			{
				drawVariableTreeNode(groupName, childNode, depth + 1, varNameToDelete);
				ImGui::TreePop();
			}

			ImGui::Unindent(depth * ImGui::GetStyle().IndentSpacing);
		}

		// Draw variables in this node
		for (auto& var : node->variables)
		{
			drawVariableRow(var, depth, varNameToDelete);
		}
	}

	void drawVariableRow(std::shared_ptr<Variable> var, int depth, std::optional<std::string>& varNameToDelete)
	{
		std::string name = var->getName();

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		ImGui::Indent(depth * ImGui::GetStyle().IndentSpacing);

		// Variable color and name
		ImGui::PushID(name.c_str());
		ImGui::ColorEdit4("##", &var->getColor().r, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::PopID();

		const bool itemIsSelected = selection.contains(name);

		if (ImGui::Selectable(var->getName().c_str(), itemIsSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap | ImGuiSelectableFlags_AllowDoubleClick))
		{
			if (ImGui::IsMouseDoubleClicked(0))
			{
				variableEditWindow->setVariableToEdit(var);
				variableEditWindow->setShowVariableEditWindowState(true);
			}

			if (ImGui::GetIO().KeyCtrl && var->getIsFound())
			{
				if (itemIsSelected)
					selection.erase(name);
				else
					selection.insert(name);
			}
			else
				selection.clear();
		}

		drawMenuVariablePopup(name, [&]()
							  { variableHandler->addNewVariable(""); }, [&](std::string name)
							  { variableHandler->addNewVariable(name); }, [&](std::string name)
							  { varNameToDelete = name; }, [&](std::string name)
							  {	variableEditWindow->setVariableToEdit(var);
                                                                variableEditWindow->setShowVariableEditWindowState(true); });

		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
		{
			if (selection.empty())
				selection.insert(name);

			/* pass a pointer to the selection as we have to call clear() on the original object upon receiving */
			std::set<std::string>* selectionPtr = &selection;
			ImGui::SetDragDropPayload("MY_DND", &selectionPtr, sizeof(selectionPtr));
			ImGui::PushID(name.c_str());
			ImGui::ColorEdit4("##", &variableHandler->getVariable(*selection.begin())->getColor().r, ImGuiColorEditFlags_NoInputs);
			ImGui::SameLine();
			ImGui::PopID();

			if (selection.size() > 1)
				ImGui::TextUnformatted("<multiple vars>");
			else
				ImGui::TextUnformatted(selection.begin()->c_str());
			ImGui::EndDragDropSource();
		}

		ImGui::TableSetColumnIndex(1);
		if (var->getIsFound())
			ImGui::Text("%s", ("0x" + std::string(GuiHelper::intToHexString(var->getAddress()))).c_str());
		else
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "NOT FOUND!");

		ImGui::TableSetColumnIndex(2);
		ImGui::Text("%s", var->getTypeStr().c_str());

		ImGui::Unindent(depth * ImGui::GetStyle().IndentSpacing);
	}

   private:
	void drawAddVariableButton()
	{
		if (ImGui::Button("Add variable", ImVec2(-1, 25 * GuiHelper::contentScale)))
		{
			variableHandler->addNewVariable("");
		}

		ImGui::BeginDisabled(projectElfPath->empty());

		if (ImGui::Button("Import variables from *.elf", ImVec2(-1, 25 * GuiHelper::contentScale)))
			importVariablesWindow->setShowImportVariablesWindow(true);

		ImGui::EndDisabled();
	}

	void drawUpdateAddressesFromElf()
	{
		static std::future<bool> refreshThread{};
		static bool shouldPopStyle = false;

		static constexpr size_t textSize = 40;
		char buttonText[textSize]{};

		if (refreshThread.valid() && refreshThread.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			snprintf(buttonText, textSize, "Update variable addresses %c", "|/-\\"[(int)(ImGui::GetTime() / 0.05f) & 3]);
		else
		{
			snprintf(buttonText, textSize, "Update variable addresses");
			if (refreshThread.valid() && !refreshThread.get())
				popup.show("Error!", "Update error. Please check the *.elf file path!", 2.0f);
		}

		ImGui::BeginDisabled(projectElfPath->empty());

		bool elfChanged = checkElfFileChanged();

		performVariablesUpdate = importVariablesWindow->shouldPerformVariableUpdate();

		if (elfChanged)
		{
			ImVec4 color = ImColor::HSV(0.1f, 0.97f, 0.72f);
			snprintf(buttonText, textSize, "Click to reload *.elf changes!");
			ImGui::PushStyleColor(ImGuiCol_Button, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);

			if (viewerDataHandler->getSettings().stopAcqusitionOnElfChange)
				viewerDataHandler->setState(DataHandlerBase::State::STOP);

			if (viewerDataHandler->getSettings().refreshAddressesOnElfChange)
			{
				performVariablesUpdate = true;
				/* TODO: examine why elf is not ready to be parsed without the delay */
				std::this_thread::sleep_for(std::chrono::milliseconds(500));
			}

			shouldPopStyle = true;
		}

		if (ImGui::Button(buttonText, ImVec2(-1, 25 * GuiHelper::contentScale)) || performVariablesUpdate)
		{
			parser->changeCurrentGDBCommand(viewerDataHandler->getSettings().gdbCommand);
			lastModifiedTime = std::filesystem::file_time_type::clock::now();
			refreshThread = std::async(std::launch::async, &GdbParser::updateVariableMap, parser, GuiHelper::convertProjectPathToAbsolute(projectElfPath, projectConfigPath));
			performVariablesUpdate = false;
		}

		/* TODO fix this ugly solution */
		if (shouldPopStyle)
		{
			ImGui::PopStyleColor(3);
			shouldPopStyle = false;
		}
		ImGui::EndDisabled();
	}

	bool checkElfFileChanged()
	{
		std::string path = GuiHelper::convertProjectPathToAbsolute(projectElfPath, projectConfigPath);
		if (!std::filesystem::exists(path))
			return false;

		auto writeTime = std::filesystem::last_write_time(path);
		return writeTime > lastModifiedTime;
	}

	void drawMenuVariablePopup(const std::string& name, std::function<void()> onNew, std::function<void(const std::string&)> onCopy, std::function<void(const std::string&)> onDelete, std::function<void(const std::string&)> onProperties)
	{
		ImGui::PushID(name.c_str());
		if (ImGui::BeginPopupContextItem(name.c_str()))
		{
			if (ImGui::MenuItem("New"))
				onNew();

			if (ImGui::MenuItem("Copy"))
				onCopy(name);

			if (ImGui::MenuItem("Delete"))
				onDelete(name);

			if (ImGui::MenuItem("Properties"))
				onProperties(name);

			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

   private:
	ViewerDataHandler* viewerDataHandler;
	PlotHandler* plotHandler;
	VariableHandler* variableHandler;
	std::string* projectElfPath;
	std::string* projectConfigPath;
	spdlog::logger* logger;

	Popup popup;
	std::shared_ptr<GdbParser> parser;
	std::shared_ptr<VariableEditWindow> variableEditWindow;
	std::shared_ptr<ImportVariablesWindow> importVariablesWindow;

	std::filesystem::file_time_type lastModifiedTime = std::filesystem::file_time_type::clock::now();

	bool performVariablesUpdate = false;
	
	// Tree view state
	bool expandAll = false;
	bool collapseAll = false;
	bool rebuildTree = true;
	
	// Variable selection state
	std::set<std::string> selection;
};