#pragma once

#include <string>
#include <vector>

#include "npc.hpp"

namespace cpptest {

enum class JobApplyResult { Hired, Rejected, AlreadyHired };
enum class TrainResult { Trained, AlreadyCertified, CannotAfford };

struct JobListing {
  std::string id;
  std::string title;
  std::vector<std::string> require_flags;
  std::string hired_flag;
};

struct Course {
  std::string id;
  std::string title;
  std::string cert_flag;
  int minutes = 120;
  int cost = 40;
  int energy = -10;
  int stress = 4;
  int unlock_die = 1;
  int unlock_face = 6;
  std::string unlock_tag = "web";
};

[[nodiscard]] JobListing HelpdeskListing();
[[nodiscard]] Course WebFundamentalsCourse();
[[nodiscard]] const JobListing* FindJobListing(const std::string& id);
[[nodiscard]] const Course* FindCourse(const std::string& id);
[[nodiscard]] JobApplyResult ApplyForJob(PlayerSave& save, const JobListing& job);
[[nodiscard]] TrainResult TakeCourse(PlayerSave& save, const Course& course);
[[nodiscard]] bool UnlockDieFace(DiceBag& bag, int die_index, int face_pips, const std::string& tag);

}  // namespace cpptest
