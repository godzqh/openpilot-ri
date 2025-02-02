#pragma once

#include <set>

#include "selfdrive/frogpilot/ui/frogpilot_functions.h"
#include "selfdrive/ui/qt/offroad/settings.h"

class FrogPilotControlsPanel : public FrogPilotListWidget {
  Q_OBJECT

public:
  explicit FrogPilotControlsPanel(SettingsWindow *parent);

signals:
  void closeParentToggle();
  void openParentToggle();

private:
  void hideEvent(QHideEvent *event);
  void hideSubToggles();
  void parentToggleClicked();
  void updateCarToggles();
  void updateMetric();
  void updateToggles();

  ButtonControl *slscPriorityButton;

  FrogPilotButtonIconControl *modelSelectorButton;

  FrogPilotDualParamControl *aggressiveProfile;
  FrogPilotDualParamControl *conditionalSpeedsImperial;
  FrogPilotDualParamControl *conditionalSpeedsMetric;
  FrogPilotDualParamControl *standardProfile;
  FrogPilotDualParamControl *relaxedProfile;

  std::set<QString> conditionalExperimentalKeys;
  std::set<QString> fireTheBabysitterKeys;
  std::set<QString> laneChangeKeys;
  std::set<QString> lateralTuneKeys;
  std::set<QString> longitudinalTuneKeys;
  std::set<QString> mtscKeys;
  std::set<QString> qolKeys;
  std::set<QString> speedLimitControllerKeys;
  std::set<QString> visionTurnControlKeys;

  std::map<std::string, ParamControl*> toggles;

  Params params;
  Params paramsMemory{"/dev/shm/params"};

  bool isMetric = params.getBool("IsMetric");
  float steerRatioStock = params.getFloat("SteerRatioStock");
  float latAccelFactorStock = params.getFloat("LatAccelFactorStock");
  float latAngleFactorStock = params.getFloat("LatAngleFactorStock");
  float frictionStock = params.getFloat("FrictionStock");
  float delayStock = params.getFloat("DelayStock");
  float offsetStock = params.getFloat("OffsetStock");

};
