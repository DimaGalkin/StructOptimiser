#pragma once

#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <format>
#include <iostream>
#include <ranges>
#include <sstream>

struct Namespace {
	unsigned long long end; // sibling address
	unsigned long long start;

	std::string decl_file;
	std::string name;

	[[nodiscard]] bool inNamespace (
		const unsigned long long loc
	) const {
		return loc > start && loc < end;
	}
};

struct Parent {
	bool is_class { false };
	unsigned long long end; // sibling address

	unsigned long long start;
	std::string decl_file;

	std::string name;

	[[nodiscard]] bool inParent (
		const unsigned long long loc
	) const {
		return loc > start && loc < end;
	}
};

struct Child {
	size_t size;
	size_t loc;
	unsigned long long at_type;
	std::string access;
	std::string decl_file;
	std::string name;

	explicit operator std::string () const noexcept { return name; }
};

struct Type {
	unsigned long long at_type;
	std::optional<size_t> byte_size;
};

std::string reduceSpaces (
	std::string_view str
);

class Optimiser {
public:
	explicit Optimiser (
		const std::string_view path
	) : path_to_optimise_ { path } {}

	static void generateDwarfDump (
		const std::string_view exe_filename,
		const std::string_view out_filename
	) {
		system(
			std::format(
				"llvm-dwarfdump {} -o {}",
				exe_filename,
				out_filename
			).c_str()
		);
	}

	void loadDwarfDump ( std::string_view filename );

	void generateChanges () {
		std::sort(parents_.begin(), parents_.end(), [](const Parent &a, const Parent &b) {
			return a.start > b.start;
		});

		std::sort(namespaces_.begin(), namespaces_.end(), [](const Namespace &a, const Namespace &b) {
			return a.start > b.start;
		});

		for (auto& p : parents_) {
			for (const auto& n : namespaces_) {
				if (n.inNamespace(p.start)) {
					p.name = std::format("{}::{}", n.name, p.name);
				}
			}

			p.name = std::format("{}@{}", p.decl_file, p.name);
		}

		namespaces_.clear();

		for (auto& c : children_) {
			c.size = collapseTypeSize(c.at_type);
		}

		type_map_.clear();

		std::vector<std::string> sorters;
		std::string clang_sorter;

		while (children_.size() > 0) {
			Child c = children_.back();
			children_.pop_back();

			for (const auto& p : parents_) {
				if (p.inParent(c.loc)) {
					if (c.access.empty()) {
						c.access = (p.is_class) ? "DW_ACCESS_private" : "DW_ACCESS_public";
					}

					if (parent_map_.contains(p.name)) {
						const auto& children = parent_map_.at(p.name);

						if (std::ranges::find_if(children,
						                         [&c](const Child& child) {
							                         return child.name == c.name;
						                         }) == children.end())
							parent_map_.at(p.name).push_back(c);
					}
					else {
						parent_map_.insert(std::make_pair(p.name, std::vector {c}));
					}

					break;
				}
			}
		}

		parents_.clear();

		for (auto &cs: parent_map_ | std::views::values) {
			const auto public_end = std::partition(cs.begin(), cs.end(), [](const Child& e) {
				return e.access == "DW_ACCESS_public";
			});

			const auto protected_end = std::partition(public_end, cs.end(), [](const Child& e) {
				return e.access == "DW_ACCESS_protected";
			});

			std::sort(cs.begin(), public_end, [](const Child& a, const Child& b) {
				return a.size < b.size;
			});

			std::sort(public_end, protected_end, [](const Child& a, const Child& b) {
				return a.size < b.size;
			});

			std::sort(protected_end, cs.end(), [](const Child& a, const Child& b) {
				return a.size < b.size;
			});
		}
	}

	void applyChanges () {
		for (const auto& [name, cs] : parent_map_) {
			std::string children;
			for (const auto& c : cs) { children.append(c.name + ","); }
			children.pop_back();

			const size_t at_pos = name.find_first_of('@');

			system (
				std::format("clang-reorder-fields --record-name={} --fields-order={} -i {} --extra-arg=-std=c++23", name.substr(at_pos + 1), children, name.substr(0, at_pos))
				.c_str()
			);
		}
	}
private:
	void parseAddressLine ( std::string_view line );

	void parseNamespaceTagLine (
		std::string_view tag,
		std::string_view data
	);

	void parseParentTagLine (
		std::string_view tag,
		std::string_view data
	);
	void parseChildTagLine (
		std::string_view tag,
		std::string_view data
	);
	void parseTypeTagLine (
		std::string_view tag,
		std::string_view data
	);

	size_t collapseTypeSize ( unsigned long long loc );

	bool processing_type_ { false };
	bool processing_child_  { false };

	bool processing_parent_ { false };
	bool processing_namespace_ { false };
	unsigned long long current_type_address_ { 0 };

	Type current_type_ {};
	std::vector<Child> children_;
	std::vector<Parent> parents_;
	std::vector<Namespace> namespaces_;

	std::string path_to_optimise_;

	std::unordered_map<unsigned long long, Type> type_map_;

	std::unordered_map<std::string, std::vector<Child>> parent_map_;
	Namespace current_namespace_ {};
	Parent current_parent_ {};
	Child current_child_ {};
};