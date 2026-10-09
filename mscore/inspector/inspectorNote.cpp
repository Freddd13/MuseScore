//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2011 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "libmscore/score.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/notevelocity.h"
#include "libmscore/synthesizerstate.h"
#include "mscore/musescore.h"
#include "libmscore/notedot.h"
#include "libmscore/beam.h"
#include "libmscore/stem.h"
#include "libmscore/hook.h"
#include "libmscore/tuplet.h"
#include "libmscore/staff.h"
#include "inspector.h"
#include "inspectorNote.h"
#include "scrubproperty.h"
#include "libmscore/playbacktiming.h"
#include "../performanceeditor/performanceeditor.h"
#include <QPushButton>
#include <QStandardItemModel>

namespace Ms {

//---------------------------------------------------------
//   InspectorNote
//---------------------------------------------------------

InspectorNote::InspectorNote(QWidget* parent)
   : InspectorElementBase(parent)
      {
      s.setupUi(addWidget());
      c.setupUi(addWidget());
      n.setupUi(addWidget());
      n.velocity->setRange(NoteVelocity::minOffset, NoteVelocity::maxOffset);

      static const NoteHead::Scheme schemes[] = {
            NoteHead::Scheme::HEAD_AUTO,
            NoteHead::Scheme::HEAD_NORMAL,
            NoteHead::Scheme::HEAD_PITCHNAME,
            NoteHead::Scheme::HEAD_PITCHNAME_NO_ACCIDENTALS,
            NoteHead::Scheme::HEAD_PITCHNAME_GERMAN,
            NoteHead::Scheme::HEAD_PITCHNAME_GERMAN_NO_ACCIDENTALS,
            NoteHead::Scheme::HEAD_SOLFEGE,
            NoteHead::Scheme::HEAD_SOLFEGE_FIXED,
            NoteHead::Scheme::HEAD_SHAPE_NOTE_4,
            NoteHead::Scheme::HEAD_SHAPE_NOTE_7_AIKIN,
            NoteHead::Scheme::HEAD_SHAPE_NOTE_7_FUNK,
            NoteHead::Scheme::HEAD_SHAPE_NOTE_7_WALKER
            };

      static const NoteHead::Group heads[] = {
            NoteHead::Group::HEAD_NORMAL,
            NoteHead::Group::HEAD_CROSS,
            NoteHead::Group::HEAD_PLUS,
            NoteHead::Group::HEAD_XCIRCLE,
            NoteHead::Group::HEAD_WITHX,
            NoteHead::Group::HEAD_TRIANGLE_UP,
            NoteHead::Group::HEAD_TRIANGLE_DOWN,
            NoteHead::Group::HEAD_SLASHED1,
            NoteHead::Group::HEAD_SLASHED2,
            NoteHead::Group::HEAD_DIAMOND,
            NoteHead::Group::HEAD_DIAMOND_OLD,
            NoteHead::Group::HEAD_CIRCLED,
            NoteHead::Group::HEAD_CIRCLED_LARGE,
            NoteHead::Group::HEAD_LARGE_ARROW,

            NoteHead::Group::HEAD_SLASH,
            NoteHead::Group::HEAD_LARGE_DIAMOND,
            NoteHead::Group::HEAD_BREVIS_ALT,

            NoteHead::Group::HEAD_HEAVY_CROSS,
            NoteHead::Group::HEAD_HEAVY_CROSS_HAT,

            NoteHead::Group::HEAD_DO,
            NoteHead::Group::HEAD_RE,
            NoteHead::Group::HEAD_MI,
            NoteHead::Group::HEAD_FA,
            NoteHead::Group::HEAD_SOL,
            NoteHead::Group::HEAD_LA,
            NoteHead::Group::HEAD_TI
            };

      //
      // fix order of noteheads
      //
      for (auto head : heads)
            n.noteHeadGroup->addItem(NoteHead::group2userName(head), int(head));

      for (auto scheme : schemes)
            n.noteHeadScheme->addItem(NoteHead::scheme2userName(scheme), int(scheme));

      // noteHeadType starts at -1: correct values and count one item more (HEAD_AUTO)
      for (int i = 0; i <= int(NoteHead::Type::HEAD_TYPES); ++i) {
            n.noteHeadType->addItem(NoteHead::type2userName(NoteHead::Type(i - 1)));
            n.noteHeadType->setItemData(i, i - 1);
            }

      // Don't let largest combo-box item determine the minimum width of Note Inspector:
      n.noteHeadScheme->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
      n.noteHeadScheme->setMinimumContentsLength(6);

      n.noteHeadGroup->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
      n.noteHeadGroup->setMinimumContentsLength(6);

      n.noteHeadType->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
      n.noteHeadType->setMinimumContentsLength(6);

      std::vector<InspectorItem> iiList = {
            { Pid::SMALL,          0, n.isSmall,       n.resetSmall         },
            { Pid::HEAD_SCHEME,    0, n.noteHeadScheme, n.resetNoteHeadScheme },
            { Pid::HEAD_GROUP,     0, n.noteHeadGroup, n.resetNoteHeadGroup },
            { Pid::HEAD_TYPE,      0, n.noteHeadType,  n.resetNoteHeadType  },
            { Pid::MIRROR_HEAD,    0, n.mirrorHead,    n.resetMirrorHead    },
            { Pid::PLAY,           0, n.play,          n.resetPlay          },
            { Pid::TUNING,         0, n.tuning,        n.resetTuning        },
            { Pid::VELO_TYPE,      0, n.velocityType,  n.resetVelocityType  },
            { Pid::VELO_OFFSET,    0, n.velocity,      n.resetVelocity      },
            { Pid::FIXED,          0, n.fixed,         n.resetFixed         },
            { Pid::FIXED_LINE,     0, n.fixedLine,     n.resetFixedLine     },

            { Pid::OFFSET,         1, c.offset,        c.resetOffset        },
            { Pid::SMALL,          1, c.isSmall,       c.resetSmall         },
            { Pid::NO_STEM,        1, c.stemless,      c.resetStemless      },
            { Pid::STEM_DIRECTION, 1, c.stemDirection, c.resetStemDirection },

            { Pid::LEADING_SPACE,  2, s.leadingSpace,  s.resetLeadingSpace  },
            };
      _gracePanel = new QWidget(c.panel);
      auto graceLayout = new QGridLayout(_gracePanel);
      auto chordLayout = qobject_cast<QGridLayout*>(c.panel->layout());
      chordLayout->addWidget(_gracePanel, chordLayout->rowCount(), 0, 1, 3);
      auto appearance = new QComboBox(_gracePanel);
      appearance->setObjectName("graceAppearance");
      for (const auto& item : std::vector<std::pair<QString, NoteType>> {
            {tr("主音"), NoteType::NORMAL}, {tr("带斜线八分"), NoteType::ACCIACCATURA},
            {tr("无斜线八分"), NoteType::APPOGGIATURA}, {tr("四分"), NoteType::GRACE4},
            {tr("十六分"), NoteType::GRACE16}, {tr("三十二分"), NoteType::GRACE32},
            {tr("后八分"), NoteType::GRACE8_AFTER}, {tr("后十六分"), NoteType::GRACE16_AFTER},
            {tr("后三十二分"), NoteType::GRACE32_AFTER}})
            {
            auto duration = TDuration::DurationType::V_EIGHTH;
            if (item.second == NoteType::GRACE4) duration = TDuration::DurationType::V_QUARTER;
            if (item.second == NoteType::GRACE16 || item.second == NoteType::GRACE16_AFTER) duration = TDuration::DurationType::V_16TH;
            if (item.second == NoteType::GRACE32 || item.second == NoteType::GRACE32_AFTER) duration = TDuration::DurationType::V_32ND;
            appearance->addItem(item.first, item.second == NoteType::NORMAL ? 0 : Chord::graceAppearanceValue(item.second, TDuration(duration)));
            }
      graceLayout->addWidget(new QLabel(tr("小音符外观"), _gracePanel), 0, 0);
      graceLayout->addWidget(appearance, 0, 1, 1, 2);
      auto position = new QComboBox(_gracePanel);
      position->setObjectName("gracePlayMode");
      position->addItems({tr("原倚音解释"), tr("拍前（主音落正拍）"), tr("拍上（延后主音）"), tr("拍后（主音末端）")});
      graceLayout->addWidget(new QLabel(tr("小音符播放位置"), _gracePanel), 1, 0);
      graceLayout->addWidget(position, 1, 1, 1, 2);
      auto unit = new QComboBox(_gracePanel);
      unit->setObjectName("graceDurationMode");
      unit->addItems({tr("每音毫秒"), tr("整组占主音比例")});
      graceLayout->addWidget(new QLabel(tr("持续时间单位"), _gracePanel), 2, 0);
      graceLayout->addWidget(unit, 2, 1, 1, 2);
      iiList.push_back({Pid::GRACE_APPEARANCE, 1, appearance, nullptr});
      iiList.push_back({Pid::GRACE_PLAY_MODE, 1, position, nullptr});
      iiList.push_back({Pid::GRACE_DURATION_MODE, 1, unit, nullptr});
      iiList.push_back(scrubProperty(_gracePanel, tr("小音符持续时间"), Pid::GRACE_DURATION, 1, 1000, 1, " ms", 1));
      auto preset = new QPushButton(tr("应用快速拍前预设"), _gracePanel);
      preset->setObjectName("graceApplyPreset");
      graceLayout->addWidget(preset, graceLayout->rowCount(), 0, 1, 3);
      connect(preset, &QPushButton::clicked, this, [this] {
            for (auto element : *inspector->el()) {
                  if (!element->isNote()) continue;
                  auto chord = toNote(element)->chord();
                  auto editor = mscore->performanceEditor();
                  if (chord->isGrace()) {
                        editor->queueProperty(chord, Pid::GRACE_APPEARANCE, Chord::graceAppearanceValue(NoteType::APPOGGIATURA, chord->durationType()));
                        chord = toChord(chord->parent());
                        }
                  editor->queueProperty(chord, Pid::GRACE_PLAY_MODE, 1);
                  editor->queueProperty(chord, Pid::GRACE_DURATION_MODE, 0);
                  editor->queueProperty(chord, Pid::GRACE_DURATION, 65.0);
                  }
            });
      _graceActual = new QLabel(_gracePanel); _graceActual->setWordWrap(true);
      graceLayout->addWidget(_graceActual, graceLayout->rowCount(), 0, 1, 3);
      const std::vector<InspectorPanel> ppList = {
            { s.title, s.panel },
            { c.title, c.panel },
            { n.title, n.panel },
            };
      mapSignals(iiList, ppList);

      connect(n.noteHeadScheme, SIGNAL(currentIndexChanged(int)), SLOT(noteHeadSchemeChanged(int)));

      connect(n.dot1,     SIGNAL(clicked()),     SLOT(dot1Clicked()));
      connect(n.dot2,     SIGNAL(clicked()),     SLOT(dot2Clicked()));
      connect(n.dot3,     SIGNAL(clicked()),     SLOT(dot3Clicked()));
      connect(n.dot4,     SIGNAL(clicked()),     SLOT(dot4Clicked()));
      connect(n.hook,     SIGNAL(clicked()),     SLOT(hookClicked()));
      connect(n.stem,     SIGNAL(clicked()),     SLOT(stemClicked()));
      connect(n.beam,     SIGNAL(clicked()),     SLOT(beamClicked()));
      connect(n.tuplet,   SIGNAL(clicked()),     SLOT(tupletClicked()));
      }

void InspectorNote::postInit()
      {
      auto element = inspector->element();
      if (!element || !element->isNote()) return;
      auto chord = toNote(element)->chord();
      bool isGrace = chord->isGrace();
      auto main = isGrace ? toChord(chord->parent()) : chord;
      _gracePanel->setVisible(isGrace || !main->graceNotes().isEmpty());
      auto appearance = _gracePanel->findChild<QComboBox*>("graceAppearance");
      appearance->setEnabled(isGrace);
      if (auto model = qobject_cast<QStandardItemModel*>(appearance->model())) model->item(0)->setEnabled(!isGrace);
      auto duration = _gracePanel->findChild<QDoubleSpinBox*>("graceDuration");
      int mode = main->getProperty(Pid::GRACE_PLAY_MODE).toInt();
      duration->setEnabled(mode != 0);
      duration->setSuffix(main->getProperty(Pid::GRACE_DURATION_MODE).toInt() ? " %" : " ms");
      _gracePanel->findChild<QComboBox*>("graceDurationMode")->setEnabled(mode != 0);
      _graceActual->setText(mode ? tr("整组实际时间：%1 ms，最多占主音一半。自定义事件及融合颤音保持原解释。")
            .arg(PlaybackTiming::graceSpanMs(main), 0, 'f', 2) : tr("沿用原倚音播放；可显式应用快速拍前预设。"));
      }

//---------------------------------------------------------
//   setElement
//---------------------------------------------------------

void InspectorNote::valueChanged(int idx, bool reset)
      {
      if (reset || iList[idx].t != Pid::VELO_TYPE) {
            InspectorElementBase::valueChanged(idx, reset);
            return;
            }
      Score* score = inspector->element()->score();
      if (score->isPlaying()) { setElement(); return; }
      score->updateVelo();
      const auto type = Note::ValueType(n.velocityType->currentIndex());
      score->startCmd();
      for (Element* element : *inspector->el()) {
            if (!element->isNote()) continue;
            Note* note = toNote(element);
            if (note->veloType() == type) continue;
            const int raw = NoteVelocity::converted(note, type, NoteVelocity::referenceBase(note, mscore ? mscore->synthesizerState().method() : 1));
            note->undoChangeProperty(Pid::VELO_TYPE, int(type));
            note->undoChangeProperty(Pid::VELO_OFFSET, raw);
            }
      score->endCmd();
      setElement();
      }

void InspectorNote::setElement()
      {
      Note* note = toNote(inspector->element());

      // Legacy grace notes may have a different written duration from their
      // original insertion type. Expose that exact value without converting it.
      auto appearance = _gracePanel->findChild<QComboBox*>("graceAppearance");
      { QSignalBlocker blocker(appearance);
        while (appearance->count() > 9) appearance->removeItem(9);
        int value = note->chord()->getProperty(Pid::GRACE_APPEARANCE).toInt();
        if (appearance->findData(value) < 0) appearance->addItem(tr("原外观（保留记谱时值）"), value); }

      { QSignalBlocker blocker(n.velocity);
        n.velocity->setRange(note->veloType() == Note::ValueType::USER_VAL ? qMin(0, note->veloOffset()) : NoteVelocity::minOffset,
              note->veloType() == Note::ValueType::USER_VAL ? qMax(127, note->veloOffset()) : qMax(NoteVelocity::maxOffset, note->veloOffset())); }
      int i = note->dots().size();
      n.dot1->setEnabled(i > 0);
      n.dot2->setEnabled(i > 1);
      n.dot3->setEnabled(i > 2);
      n.dot4->setEnabled(i > 3);
      n.stem->setEnabled(note->chord()->stem());
      n.hook->setEnabled(note->chord()->hook());
      n.beam->setEnabled(note->chord()->beam());
      n.tuplet->setEnabled(note->chord()->tuplet());

      InspectorElementBase::setElement();

      //must be placed after InspectorBase::setElement() cause the last one sets resetButton enability
      if (note->staffType()->group() == StaffGroup::STANDARD) {
            n.noteHeadScheme->setEnabled(true);
            noteHeadSchemeChanged(n.noteHeadScheme->currentIndex());
            }
      else {
            n.noteHeadScheme->setEnabled(false);
            n.resetNoteHeadScheme->setEnabled(false);
            n.noteHeadGroup->setEnabled(false);
            n.resetNoteHeadGroup->setEnabled(false);
            }

      bool nograce = !note->chord()->isGrace();
      s.leadingSpace->setEnabled(nograce);
      s.resetLeadingSpace->setEnabled(nograce && s.leadingSpace->value());

      n.fixedLine->setEnabled(n.fixed->isChecked());
      n.playWidget->setVisible(n.play->isChecked());
      }

//---------------------------------------------------------
//   noteHeadSchemeChanged
//---------------------------------------------------------

void InspectorNote::noteHeadSchemeChanged(int index)
      {
      Note* note = toNote(inspector->element());
      NoteHead::Scheme scheme = (index == 0 ? note->staffType()->noteHeadScheme() : NoteHead::Scheme(index - 1));
      if (scheme == NoteHead::Scheme::HEAD_NORMAL) {
            n.noteHeadGroup->setEnabled(true);
            n.resetNoteHeadGroup->setEnabled(note->headGroup() != NoteHead::Group::HEAD_NORMAL);
            }
      else {
            n.noteHeadGroup->setEnabled(false);
            n.resetNoteHeadGroup->setEnabled(false);
            }
      }

//---------------------------------------------------------
//   dot1Clicked
//---------------------------------------------------------

void InspectorNote::dot1Clicked()
      {
      Note* note = toNote(inspector->element());
      if (note == 0)
            return;
      if (note->dots().size() > 0) {
            NoteDot* dot = note->dot(0);
            dot->score()->select(dot);
            dot->score()->update();
            inspector->update();
            }
      }

//---------------------------------------------------------
//   dot2Clicked
//---------------------------------------------------------

void InspectorNote::dot2Clicked()
      {
      Note* note = toNote(inspector->element());
      if (note == 0)
            return;
      if (note->dots().size() > 1) {
            NoteDot* dot = note->dot(1);
            dot->score()->select(dot);
            dot->score()->update();
            inspector->update();
            }
      }

//---------------------------------------------------------
//   dot3Clicked
//---------------------------------------------------------

void InspectorNote::dot3Clicked()
      {
      Note* note = toNote(inspector->element());
      if (note == 0)
            return;
      if (note->dots().size() > 2) {
            NoteDot* dot = note->dot(2);
            dot->score()->select(dot);
            dot->score()->update();
            inspector->update();
            }
      }

//---------------------------------------------------------
//   dot4Clicked
//---------------------------------------------------------

void InspectorNote::dot4Clicked()
      {
      Note* note = toNote(inspector->element());
      if (note == 0)
            return;
      if (note->dots().size() > 3) {
            NoteDot* dot = note->dot(3);
            dot->score()->select(dot);
            dot->score()->update();
            inspector->update();
            }
      }

//---------------------------------------------------------
//   hookClicked
//---------------------------------------------------------

void InspectorNote::hookClicked()
      {
      Note* note = toNote(inspector->element());
      if (note == 0)
            return;
      Hook* hook = note->chord()->hook();
      if (hook) {
            note->score()->select(hook);
            note->score()->update();
            inspector->update();
            }
      }

//---------------------------------------------------------
//   stemClicked
//---------------------------------------------------------

void InspectorNote::stemClicked()
      {
      Note* note = toNote(inspector->element());
      if (note == 0)
            return;
      Stem* stem = note->chord()->stem();
      if (stem) {
            note->score()->select(stem);
            note->score()->update();
            inspector->update();
            }
      }

//---------------------------------------------------------
//   beamClicked
//---------------------------------------------------------

void InspectorNote::beamClicked()
      {
      Note* note = toNote(inspector->element());
      if (note == 0)
            return;
      Beam* beam = note->chord()->beam();
      if (beam) {
            note->score()->select(beam);
            note->score()->update();
            inspector->update();
            }
      }

//---------------------------------------------------------
//   tupletClicked
//---------------------------------------------------------

void InspectorNote::tupletClicked()
      {
      Note* note = toNote(inspector->element());
      if (note == 0)
            return;
      Tuplet* tuplet = note->chord()->tuplet();
      if (tuplet) {
            note->score()->select(tuplet);
            note->score()->update();
            inspector->update();
            }
      }

}
