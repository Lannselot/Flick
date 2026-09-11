# Application process suite inventory

Before this split, all 52 scenarios ran in `flick.application` from
`flick_application_test.cpp`. They now run in the following independently selectable CTest suites.
No scenario was added, removed, or renamed.

## `flick.application.browsing` (19)

- `displaysPngInTopLevelWindow`
- `displaysJpegInTopLevelWindow`
- `displaysStableEmptyStateWithoutImage`
- `presentsCoherentEmptyErrorAndLargeImageStates`
- `delaysLoadingPresentationAndClearsPreviousImage`
- `validDragFeedbackRestoresThePreviousPresentation`
- `separateInvocationsRemainIndependent`
- `browsesNaturallySortedVisibleSupportedImages`
- `opensSelectedImageWithCtrlO`
- `singleImageDropBrowsesContainingDirectory`
- `multipleImageDropBrowsesOnlySupportedDroppedFilesInNaturalOrder`
- `directorySequenceTracksExternalFilesystemChanges`
- `explicitListIgnoresExternalDirectoryAdditions`
- `cancelledPickerAndUnsupportedDropRemainStable`
- `decodingRemainsResponsiveAndStaleResultsAreIgnored`
- `prefetchedImagesAreReusedAndCacheIsBounded`
- `decodeFailureExplainsTheProblemAndKeepsNavigationUsable`
- `technicalDetailsRemainSecondaryAndEnterRecoversAfterRepair`
- `extremeDimensionsRequireConfirmationBeforeBackgroundDecode`

## `flick.application.presentation` (14)

- `rendersSupportedStaticFormatsAndTransparency`
- `honorsEmbeddedProfilesAndDefaultsUntaggedImagesToSrgb`
- `updatesRenderingWhenTheDisplayProfileChanges`
- `appliesExifOrientation`
- `appliesInitialScalingAndKeyboardZoomModes`
- `highZoomRemainsResponsiveWithoutAllocatingTheFullScaledImage`
- `pointerZoomKeepsCursorOnTheSameImagePoint`
- `pansByDragAndShiftArrowsWhilePlainArrowsNavigateAndResetView`
- `wheelActionDefaultsToNavigationWithCtrlZoom`
- `wheelActionCanSwitchToZoomWithCtrlNavigation`
- `temporarilyRotatesCurrentViewAndResetsOnNavigation`
- `transientStatusReportsViewContextAndReappearsOnMouseMovement`
- `statusOverlayElidesLongNamesAndRestoresContextAfterFeedback`
- `informationDialogStaysLiveWhileBrowsing`

## `flick.application.settings` (3)

- `settingsApplyImmediatelyAndPersistAcrossLaunches`
- `testHarnessUsesExplicitSettingsRoot`
- `settingsDialogPreviewsCommitsRollsBackAndResets`

## `flick.application.commands` (6)

- `copiesPathAndRenderedImageAndExposesContextCommands`
- `exposesGroupedCommandSurfacesWithoutNavigationRows`
- `quitCommandIsSharedAndExitsCleanly`
- `contextMenuStaysReachableNearEveryScreenEdge`
- `restoresViewingFocusAndAppliesEscapePrecedence`
- `exposesAccessibleKeyboardActions`

## `flick.application.animation` (4)

- `animatedGifPreservesTimingAndFiniteLoop`
- `animatedWebpPreservesTimingAndLoops`
- `spacePausesAndResumesAnimationButDoesNotAffectStaticImages`
- `informationDialogReportsNaturalAnimationCompletion`

## `flick.application.platform` (6)

- `togglesFullscreenFromKeyboardAndPointer`
- `firstUseTeachingPersistsAfterBrowsingIsLearned`
- `fullscreenTeachingAppearsOnlyOnFirstEntry`
- `fullscreenInactivityHidesStatusAndPointerWithoutBlockingKeyboard`
- `revealsCurrentFileAndReportsExternalActionFailures`
- `usesViewingSurfaceVocabularyAndMotionContract`
