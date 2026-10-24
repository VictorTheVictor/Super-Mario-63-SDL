onClipEvent (load) {
	speed = 3;
}
onClipEvent (enterFrame) {
	// move right until the edge
	if (Key.isDown(Key.RIGHT) && this._x < Stage.width) {
		_x += speed;
	} else if (_x >= 450) {
		_parent.reachedEdge = true;
	}
}
