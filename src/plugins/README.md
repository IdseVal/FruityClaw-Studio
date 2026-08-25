# src/plugins — reserved, post-MVP

VST3 and CLAP host adapters land here and nowhere else. This is the only
directory that may include a VST3 or CLAP header (architecture-seams rule 3,
ADR-008 Processor rule 4). Third-party Plugin hosting is post-MVP
(core document section 3.6); the directory exists so the first developer to
reach for a plugin SDK header has somewhere obvious to put it and no excuse
for putting it elsewhere.
