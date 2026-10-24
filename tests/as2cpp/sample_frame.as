stop();
// Spawn helper for a test level
var count = 0;
_root.spawnStar = function (x, y) {
	var star = _root.attachMovie("Star", "star" + count, _root.getNextHighestDepth(), {_x:x, _y:y});
	count++;  // timeline variable
	star.onEnterFrame = function () {
		this._rotation += 5;
		if (this._rotation > 90 && !_root.paused) {
			this.removeMovieClip();
		}
	};
	return star;
};

function makeCounter() {
	var n = 0;
	return function () {
		n += 1;
		return n;
	};
}

switch (_root.mode) {
	case "title":
		gotoAndStop("menu");
		break;
	case 2:
	default:
		trace("mode " + _root.mode + " / " + 1/2);
}
for (var k in _root.settings) {
	trace(k + "=" + _root.settings[k]);
}
tellTarget ("_root.hud") {
	gotoAndPlay(2);
}
label = typeof label == "undefined" ? "none" : label;
