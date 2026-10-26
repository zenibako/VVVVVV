module github.com/zenibako/pocket-music

go 1.25.0

require github.com/jfreymuth/oggvorbis v1.0.5

require github.com/jfreymuth/vorbis v1.0.2 // indirect

replace github.com/jfreymuth/vorbis => ./third_party/vorbis

replace github.com/jfreymuth/oggvorbis => ./third_party/oggvorbis
