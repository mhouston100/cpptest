#include "job.hpp"

#include "day.hpp"
#include "dice.hpp"

namespace cpptest {
namespace {

const JobListing kHelpdesk{
    "helpdesk",
    "Helpdesk technician",
    {"exp_web", "cert_web"},
    "job_helpdesk",
};

const Course kWebCourse{
    "web_fundamentals",
    "Web fundamentals",
    "cert_web",
    120,
    40,
    -10,
    4,
    1,
    6,
    "web",
};

}  // namespace

JobListing HelpdeskListing() {
  return kHelpdesk;
}

Course WebFundamentalsCourse() {
  return kWebCourse;
}

const JobListing* FindJobListing(const std::string& id) {
  if (id == kHelpdesk.id) {
    return &kHelpdesk;
  }
  return nullptr;
}

const Course* FindCourse(const std::string& id) {
  if (id == kWebCourse.id) {
    return &kWebCourse;
  }
  return nullptr;
}

JobApplyResult ApplyForJob(PlayerSave& save, const JobListing& job) {
  if (job.hired_flag.empty()) {
    return JobApplyResult::Rejected;
  }
  if (GetFlag(save, job.hired_flag)) {
    return JobApplyResult::AlreadyHired;
  }
  for (const auto& flag : job.require_flags) {
    if (!GetFlag(save, flag)) {
      return JobApplyResult::Rejected;
    }
  }
  SetFlag(save, job.hired_flag, true);
  return JobApplyResult::Hired;
}

bool UnlockDieFace(DiceBag& bag, int die_index, int face_pips, const std::string& tag) {
  if (tag.empty() || die_index < 0 || face_pips < 1 || face_pips > kDieFaces) {
    return false;
  }
  if (die_index >= static_cast<int>(bag.dice.size())) {
    return false;
  }
  bag.dice[static_cast<size_t>(die_index)].tags[static_cast<size_t>(face_pips - 1)] = tag;
  return true;
}

TrainResult TakeCourse(PlayerSave& save, const Course& course) {
  if (!course.cert_flag.empty() && GetFlag(save, course.cert_flag)) {
    return TrainResult::AlreadyCertified;
  }
  if (save.money < course.cost) {
    return TrainResult::CannotAfford;
  }
  EnsureDiceBag(save);
  ApplyDayDelta(save, course.minutes, course.energy, course.stress, -course.cost);
  if (!course.cert_flag.empty()) {
    SetFlag(save, course.cert_flag, true);
  }
  static_cast<void>(
      UnlockDieFace(save.dice, course.unlock_die, course.unlock_face, course.unlock_tag));
  return TrainResult::Trained;
}

}  // namespace cpptest
