#include "TODSystem.h"

#include <implot.h>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TODCurve::Node, time, value)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TODCurve, nodes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TODProfile, curves)

// NOTE (2024-02-03, Cat): reranking can be optimised by abstracting insertion and deletion, but I am lazy.
VOID TODCurve::rerank()
{
	sorted_indices.clear();
	for (size_t i = 0; i < nodes.size(); ++i)
		sorted_indices.push_back(i);
	std::ranges::sort(sorted_indices, [&](size_t a, size_t b) { return nodes[a] < nodes[b]; });
}

float TODCurve::query(float t, bool cache)
{
	assert(t >= 0 && t <= 1);

	if (nodes.empty())
		return 0;

	cached_query = std::min(cached_query, nodes.size() - 1);

	// find interval
	size_t rank_l = cached_query;
	// NOTE (2024-02-03, Cat): can do this with a custom iterator that returns pairs of nodes i.e. intervals
	while (true) {
		size_t rank_r = (rank_l + 1) % nodes.size();

		float time_l = nodes[sorted_indices[rank_l]].time;
		float time_r = nodes[sorted_indices[rank_r]].time;

		bool inbetween = false;
		if (rank_r == 0)
			inbetween = (t >= time_l) || (t <= time_r);
		else
			inbetween = (t >= time_l) && (t <= time_r);
		if (inbetween)
			break;

		rank_l = rank_r;
	}
	if (cache)
		cached_query = rank_l;

	// interpolate
	auto& node_l = nodes[sorted_indices[rank_l]];
	auto& node_r = nodes[sorted_indices[(rank_l + 1) % nodes.size()]];
	if (t == node_l.time)
		return node_l.value;
	if (t == node_r.time)
		return node_r.value;

	float interval_len = node_r.time - node_l.time;
	interval_len += &node_l == &node_r;
	interval_len += interval_len < 0;
	float dist_to_l = t - node_l.time;
	dist_to_l += dist_to_l < 0;

	return std::lerp(node_l.value, node_r.value, dist_to_l / interval_len);
}

json TODProfile::query(float t)
{
	json query_result = {};
	for (auto& [feature, parameters] : curves) {
		if (!query_result.contains(feature))
			query_result[feature] = {};

		for (auto& [parameter, curve] : parameters)
			query_result[feature][parameter] = curve.query(t);
	}
	return query_result;
}

TODSystem::TODSystem(json& param_list)
{
}

void TODSystem::drawCurveEditor(TODCurve& curve, std::optional<float> t)
{
	if (t.has_value())
		10;

	static size_t selected_node_idx = 0;
	bool need_rerank = false;

	{
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Add:");
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (ImGui::Button("Sunrise")) {
			need_rerank = true;
			curve.nodes.push_back({ .25, curve.query(.25, false) });
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (ImGui::Button("Midday")) {
			need_rerank = true;
			curve.nodes.push_back({ .5, curve.query(.5, false) });
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (ImGui::Button("Sunset")) {
			need_rerank = true;
			curve.nodes.push_back({ .75, curve.query(.75, false) });
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (ImGui::Button("Midnight")) {
			need_rerank = true;
			curve.nodes.push_back({ 1, curve.query(1, false) });
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("|");
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (ImGui::Button("Clear")) {
			need_rerank = true;
			curve.nodes.clear();
		}

		if (need_rerank) {
			selected_node_idx = curve.nodes.size() - 1;
			curve.rerank();
		}
	}
	selected_node_idx = std::min(selected_node_idx, curve.nodes.size() - 1);

	ImGui::Separator();

	ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, { 0, .1 });
	if (ImPlot::BeginPlot("TOD Curve")) {
		ImPlot::SetupAxisLimits(ImAxis_X1, 0, 1, ImPlotCond_Always);
		ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_AutoFit);

		std::vector<float> pts_time, pts_value;

		float edge_value = curve.query(1.0, false);
		pts_time.push_back(0);
		pts_value.push_back(edge_value);

		for (const auto& idx : curve.sorted_indices) {
			pts_time.push_back(curve.nodes[idx].time);
			pts_value.push_back(curve.nodes[idx].value);
		}

		pts_time.push_back(1);
		pts_value.push_back(edge_value);

		if (t.has_value()) {
			ImPlot::PushStyleColor(ImPlotCol_Line, { 1, 1, 1, .5 });
			ImPlot::PopStyleColor();
		}

		ImPlot::PlotLine("##line", pts_time.data(), pts_value.data(), (int)pts_time.size());
		if (!curve.nodes.empty()) {
			ImPlot::PlotScatter("##Points", pts_time.data() + 1, pts_value.data() + 1, (int)curve.nodes.size());

			ImPlot::PushStyleVar(ImPlotStyleVar_MarkerSize, 8);
			ImPlot::PlotScatter("Selected", &curve.nodes[selected_node_idx].time, &curve.nodes[selected_node_idx].value, 1);
			ImPlot::PopStyleVar();
		}

		ImPlot::EndPlot();
	}
	ImPlot::PopStyleVar();

	ImGui::Separator();

	if (curve.nodes.empty())
		ImGui::TextDisabled("No node present.");
	else {
		need_rerank = false;

		ImGui::AlignTextToFramePadding();
		ImGui::Text("Select Node:");
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (ImGui::ArrowButton("prevnode", ImGuiDir_Left)) {
			size_t selected_node_rank = std::ranges::find(curve.sorted_indices, selected_node_idx) - curve.sorted_indices.begin();
			auto prev_rank = (selected_node_rank == 0 ? curve.nodes.size() : selected_node_rank) - 1;
			selected_node_idx = curve.sorted_indices[prev_rank];
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (ImGui::ArrowButton("nextnode", ImGuiDir_Right)) {
			size_t selected_node_rank = std::ranges::find(curve.sorted_indices, selected_node_idx) - curve.sorted_indices.begin();
			auto next_rank = (selected_node_rank == curve.nodes.size() - 1 ? 0 : selected_node_rank) + 1;
			selected_node_idx = curve.sorted_indices[next_rank];
		}

		auto& node = curve.nodes[selected_node_idx];
		if (ImGui::BeginTable("Feature Table", 2, ImGuiTableFlags_SizingStretchSame)) {
			ImGui::TableNextColumn();
			if (ImGui::SliderFloat("Time", &node.time, 0, 1, "%.2f"))
				need_rerank = true;
			ImGui::TableNextColumn();

			ImGui::TableNextColumn();
			ImGui::InputFloat("Value", &node.value, 0.f, 0.f, "%.4f");

			ImGui::EndTable();
		}

		if (need_rerank)
			curve.rerank();
	}
}