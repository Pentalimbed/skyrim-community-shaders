#pragma once

struct TODCurve
{
	struct Node
	{
		float time = .5;
		float value = 0;

		auto operator<=>(const Node&) const = default;
	};

	std::vector<Node> nodes = {};
	std::vector<size_t> sorted_indices = {};
	size_t cached_query = 0;

	float query(float t, bool cache = true);  // non-cached query used only by imgui

	void rerank();
};

struct TODProfile
{
	std::unordered_map<std::string, std::unordered_map<std::string, TODCurve>> curves;

	json query(float t);
};

struct TODSystem
{
	std::unordered_map<std::string, std::string> valid_params;
	TODProfile default_profile;

	TODSystem(json& param_list);
	void drawCurveEditor(TODCurve& curve, std::optional<float> t);
};