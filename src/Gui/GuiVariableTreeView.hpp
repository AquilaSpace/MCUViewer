#pragma once

#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <functional>

#include "../commons.hpp"
#include "GuiHelper.hpp"
#include "ImguiPlugins.hpp"
#include "Variable.hpp"

/**
 * @brief Shared tree view component for hierarchical variable display
 * 
 * This component provides a reusable tree view implementation that can be used
 * by different variable selection/import dialogs. It handles:
 * - Hierarchical namespace organization (:: and . separators)
 * - Search filtering with tree rebuilding
 * - Expand/collapse functionality
 * - Group and individual selection
 * - Customizable data sources and selection callbacks
 */
template<typename T>
class VariableTreeView
{
public:
    struct TreeNode
    {
        std::map<std::string, std::shared_ptr<TreeNode>> children;
        std::vector<T> items;
    };

    // Function types for customization
    using NameExtractor = std::function<std::string(const T&)>;
    using AddressExtractor = std::function<std::string(const T&)>;
    using SelectionChecker = std::function<bool(const T&)>;
    using SelectionToggler = std::function<void(const T&, bool)>;
    using ItemFilter = std::function<bool(const T&, const std::string&)>;

    VariableTreeView(
        NameExtractor nameExtractor,
        AddressExtractor addressExtractor,
        SelectionChecker selectionChecker,
        SelectionToggler selectionToggler,
        ItemFilter itemFilter = nullptr
    ) : nameExtractor(nameExtractor),
        addressExtractor(addressExtractor), 
        selectionChecker(selectionChecker),
        selectionToggler(selectionToggler),
        itemFilter(itemFilter ? itemFilter : defaultItemFilter)
    {
    }

    void draw(const std::vector<T>& items, const std::string& searchFilter, float tableHeight = 400.0f, bool showControls = true)
    {
        // Check if we need to rebuild the tree
        if (lastSearchString != searchFilter || lastItemCount != items.size() || forceRebuild)
        {
            buildTree(items, searchFilter);
            lastSearchString = searchFilter;
            lastItemCount = items.size();
            forceRebuild = false;
        }

        // Tree controls (optional)
        if (showControls)
        {
            ImGui::Text("Tree View:");
            ImGui::SameLine();
            if (ImGui::Button("Expand All"))
            {
                expandAllFlag = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Collapse All"))
            {
                collapseAllFlag = true;
            }
        }

        static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | 
                                      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | 
                                      ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("variable_tree", 3, flags, ImVec2(0.0f, tableHeight)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 20 * GuiHelper::contentScale); // Checkbox
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80 * GuiHelper::contentScale);
            ImGui::TableHeadersRow();

            // Draw tree
            if (rootNode)
            {
                drawTreeNode("Root", rootNode, 0);
            }

            ImGui::EndTable();
        }

        // Reset expand/collapse flags
        expandAllFlag = false;
        collapseAllFlag = false;
    }

    void rebuildTree() { forceRebuild = true; }
    
    void expandAll() { expandAllFlag = true; }
    void collapseAll() { collapseAllFlag = true; }

private:
    static bool defaultItemFilter(const T& item, const std::string& searchFilter)
    {
        // Default implementation that always returns true (no filtering)
        return true;
    }

    void resetCheckboxPosition()
    {
        // Reset cursor to column start with consistent padding
        ImVec2 cellStart = ImGui::GetCursorPos();
        cellStart.x = ImGui::GetCursorStartPos().x + 4.0f; // 4px padding
        ImGui::SetCursorPos(cellStart);
    }

    void buildTree(const std::vector<T>& items, const std::string& searchFilter)
    {
        rootNode = std::make_shared<TreeNode>();

        for (const auto& item : items)
        {
            std::string name = nameExtractor(item);
            
            // Apply search filter
            if (!searchFilter.empty() && !itemFilter(item, searchFilter))
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
            auto currentNode = rootNode;
            for (size_t i = 0; i < parts.size() - 1; ++i)
            {
                const std::string& group = parts[i];
                if (currentNode->children.find(group) == currentNode->children.end())
                {
                    currentNode->children[group] = std::make_shared<TreeNode>();
                }
                currentNode = currentNode->children[group];
            }

            // Add item to the appropriate node
            currentNode->items.push_back(item);
        }
    }

    void drawTreeNode(const std::string& nodeName, std::shared_ptr<TreeNode> node, int depth)
    {
        if (!node) return;

        // Draw child groups first
        for (auto& [groupName, childNode] : node->children)
        {
            if (!childNode) continue;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            resetCheckboxPosition();

            // Group checkbox (select all in group)
            bool groupSelected = areAllItemsInNodeSelected(childNode);
            bool groupPartial = areAnyItemsInNodeSelected(childNode) && !groupSelected;
            
            if (groupPartial)
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            
            if (ImGui::Checkbox(("##group_" + groupName).c_str(), &groupSelected))
                selectAllItemsInNode(childNode, groupSelected);
            
            if (groupPartial)
                ImGui::PopItemFlag();

            ImGui::TableSetColumnIndex(1);

            // Add hierarchical spacing to name column
            ImGui::Dummy(ImVec2(depth * ImGui::GetStyle().IndentSpacing, 0.0f));
            ImGui::SameLine();

            // Group tree node with expand/collapse functionality
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            std::string nodeId = "##tree_" + groupName + "_" + std::to_string(depth);
            
            // Handle programmatic expand/collapse
            if (expandAllFlag)
                ImGui::SetNextItemOpen(true);
            else if (collapseAllFlag)
                ImGui::SetNextItemOpen(false);

            bool nodeOpen = ImGui::TreeNodeEx((groupName + nodeId).c_str(), flags, "%s", groupName.c_str());
            
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(""); // Empty address column for groups

            if (nodeOpen)
            {
                drawTreeNode(groupName, childNode, depth + 1);
                ImGui::TreePop();
            }
        }

        // Draw items in this node
        for (auto& item : node->items)
        {
            drawItemRow(item, depth);
        }
    }

    void drawItemRow(const T& item, int depth)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        resetCheckboxPosition();

        // Item checkbox
        std::string name = nameExtractor(item);
        bool isSelected = selectionChecker(item);
        if (ImGui::Checkbox(("##item_" + name).c_str(), &isSelected))
            selectionToggler(item, isSelected);

        ImGui::TableSetColumnIndex(1);

        // Add hierarchical spacing to name column
        ImGui::Dummy(ImVec2(depth * ImGui::GetStyle().IndentSpacing, 0.0f));
        ImGui::SameLine();

        // Item name (clickable)
        ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick;
        if (ImGui::Selectable(name.c_str(), isSelected, selectable_flags, ImVec2(0, 0)))
        {
            if (ImGui::GetIO().KeyCtrl)
                selectionToggler(item, !isSelected);
            else
                selectionToggler(item, true);

            if (ImGui::IsMouseDoubleClicked(0))
            {
                // Double-click behavior - could add callback for this
            }
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%s", addressExtractor(item).c_str());
    }

    bool areAllItemsInNodeSelected(std::shared_ptr<TreeNode> node)
    {
        if (!node) return false;

        // If node has no items and no children, consider it "selected" (empty groups)
        if (node->items.empty() && node->children.empty()) return true;

        // Check direct items
        for (auto& item : node->items)
        {
            if (!selectionChecker(item))
                return false;
        }

        // Check child nodes recursively
        for (auto& [groupName, childNode] : node->children)
        {
            if (!areAllItemsInNodeSelected(childNode))
                return false;
        }

        return true;
    }

    bool areAnyItemsInNodeSelected(std::shared_ptr<TreeNode> node)
    {
        if (!node) return false;

        // Check direct items
        for (auto& item : node->items)
        {
            if (selectionChecker(item))
                return true;
        }

        // Check child nodes recursively
        for (auto& [groupName, childNode] : node->children)
        {
            if (areAnyItemsInNodeSelected(childNode))
                return true;
        }

        return false;
    }

    void selectAllItemsInNode(std::shared_ptr<TreeNode> node, bool selected)
    {
        if (!node) return;

        // Select/deselect direct items
        for (auto& item : node->items)
        {
            selectionToggler(item, selected);
        }

        // Select/deselect child nodes recursively
        for (auto& [groupName, childNode] : node->children)
        {
            selectAllItemsInNode(childNode, selected);
        }
    }

private:
    NameExtractor nameExtractor;
    AddressExtractor addressExtractor;
    SelectionChecker selectionChecker;
    SelectionToggler selectionToggler;
    ItemFilter itemFilter;

    std::shared_ptr<TreeNode> rootNode;
    std::string lastSearchString;
    size_t lastItemCount = 0;
    bool forceRebuild = true;
    
    // Tree view state
    bool expandAllFlag = false;
    bool collapseAllFlag = false;
};