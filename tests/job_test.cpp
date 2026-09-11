#include "job.hpp"

#include "day.hpp"
#include "dice.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int g_fails = 0;

void Expect(const bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_fails;
  }
}

}  // namespace

int main() {
  using namespace cpptest;

  {
    PlayerSave save{};
    EnsureDiceBag(save);
    Expect(ApplyForJob(save, HelpdeskListing()) == JobApplyResult::Rejected,
           "no cert and no experience is rejected");
  }
  {
    PlayerSave save{};
    EnsureDiceBag(save);
    SetFlag(save, "exp_web", true);
    Expect(ApplyForJob(save, HelpdeskListing()) == JobApplyResult::Rejected,
           "experience without cert is rejected");
  }
  {
    PlayerSave save{};
    EnsureDiceBag(save);
    SetFlag(save, "cert_web", true);
    Expect(ApplyForJob(save, HelpdeskListing()) == JobApplyResult::Rejected,
           "cert without experience is rejected");
  }
  {
    PlayerSave save{};
    EnsureDiceBag(save);
    SetFlag(save, "exp_web", true);
    SetFlag(save, "cert_web", true);
    Expect(ApplyForJob(save, HelpdeskListing()) == JobApplyResult::Hired, "cert + experience is hired");
    Expect(GetFlag(save, "job_helpdesk"), "hired flag is set");
    Expect(ApplyForJob(save, HelpdeskListing()) == JobApplyResult::AlreadyHired,
           "second apply is already hired");
  }
  {
    PlayerSave save{};
    EnsureDiceBag(save);
    save.money = 10;
    Expect(TakeCourse(save, WebFundamentalsCourse()) == TrainResult::CannotAfford,
           "course is blocked when broke");
    Expect(!GetFlag(save, "cert_web"), "broke course does not grant cert");
  }
  {
    PlayerSave save{};
    EnsureDiceBag(save);
    save.money = 50;
    Expect(TakeCourse(save, WebFundamentalsCourse()) == TrainResult::Trained, "course trains");
    Expect(GetFlag(save, "cert_web"), "course grants cert_web");
    Expect(save.money == 10, "course costs $40");
    Expect(DieFaceTag(save.dice.dice[1], 6) == "web", "course unlocks web on die 2 face 6");
    Expect(TakeCourse(save, WebFundamentalsCourse()) == TrainResult::AlreadyCertified,
           "repeat course is blocked");
  }

  if (g_fails != 0) {
    std::fprintf(stderr, "%d job tests failed\n", g_fails);
    return EXIT_FAILURE;
  }
  std::puts("job tests passed");
  return EXIT_SUCCESS;
}
