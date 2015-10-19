/*
  mcpe_viz openlayers viewer
  by Plethora777 - 2015.10.08

  todo

  * todobig -- Chrome appears to be demented about serving local files.  You get CORS errors.  Not at all clear that this can be resolved w/o really ugly workarounds (e.g. disabling chrome security); This could be the case with MS Edge on win10 also.
  -- http://stackoverflow.com/questions/3102819/disable-same-origin-policy-in-chrome
  -- https://chrome.google.com/webstore/detail/allow-control-allow-origi/nlfbmbojpeacfghkpbjhddihlkkiljbi?hl=en

  * todohere -- some mods in test-all2 -- combine raw layer + regular layer selector; experiments w/ multilevel dropdown for mobs

  * goto X -- e.g. Player; World Origin; World Spawn; Player Spawn; etc
  
  * improve the ui - it's sorta clunky :)

  * some reporting of details in geojson -- counts of different items?

  * online help/intro text -- bootstrap tour? (is there a CDN for bstour?)

  * measuring tool

  * drawing tools on map -- especially circles (show diameter) (for mob systems)

  * idea from /u/sturace -- filter by pixel value (e.g. show only oak trees)

  * toggle btwn overworld / nether - show player est. spot in nether even if they are in overworld
  -- some tool to help w/ planning portals (e.g. mark a spot in overworld and show where it maps in nether)

  * when in raw layer mode, auto load layer before/after the layer that gets loaded? (improve perceived speed)

  * how to get navbar + popovers in full screen?

  * web: why does click on point not always work?

  * ability to hover over a pixel and get info (e.g. "Jungle Biome - Watermelon @ (X,Z,Y)"); switch image layers
  -- hidden layers for more info in mouse pos window: biomes + height

  * rewrite/adapt to use the google closure tools?

  */


/*
  todo - interesting openlayers examples:

  vector icons
  http://openlayers.org/en/v3.9.0/examples/icon.html

  measuring
  http://openlayers.org/en/v3.9.0/examples/measure.html

  nav controls
  http://openlayers.org/en/v3.9.0/examples/navigation-controls.html

  overview map
  http://openlayers.org/en/v3.9.0/examples/overviewmap.html
  http://openlayers.org/en/v3.9.0/examples/overviewmap-custom.html
*/

// todo - do a separate geojson file for nether + overworld?

// todo - it might be cool to use ONE projection for both overworld and nether -- nether would auto-adjust?


var map = null, projection = null, extent = null;
var popover = null;

var globalMousePosition = null;
var pixelData = null, pixelDataName = '';

var layerRawIndex = 63;
var layerMain = null, srcLayerMain = null;

var mousePositionControl = null;

var globalDimensionId = -1;
var globalLayerMode = 0, globalLayerId = 0;

var listEntityToggle = [];
var listTileEntityToggle = [];

var globalCORSWarning = 'MCPE Viz Hint: If you are loading files from the local filesystem, your browser might not be allowing us to load additional files or examine pixels in maps.  Firefox does not have this limitation.  See README.md for more info...';
var globalCORSWarningFlag = false;

// this removes the hideous blurriness when zoomed in
var setCanvasSmoothingMode = function(evt) {
    evt.context.mozImageSmoothingEnabled = false;
    evt.context.webkitImageSmoothingEnabled = false;
    evt.context.msImageSmoothingEnabled = false;
    evt.context.imageSmoothingEnabled = false;
};
var resetCanvasSmoothingMode = function(evt) {
    evt.context.mozImageSmoothingEnabled = true;
    evt.context.webkitImageSmoothingEnabled = true;
    evt.context.msImageSmoothingEnabled = true;
    evt.context.imageSmoothingEnabled = true;
};


var loadEventCount = 0;
function updateLoadEventCount(delta) {
    loadEventCount += delta;
    if (loadEventCount < 0) { loadEventCount = 0; }
    
    if (loadEventCount > 0) {
        var pos = $('#map').offset();
        var x1 = pos.left + 70;
        var y1 = pos.top + 20;
        $('#throbber')
            .css({ position: 'absolute', left: x1, top: y1 })
            .show();
	
        var a = [];
        if (loadEventCount > 0) {
	    a.push('Layers remaining: ' + loadEventCount);
	}
        $('#throbber-msg').html(a.join('; '));
    } else {
        $('#throbber').hide();
    }
}

function doParsedItem(obj, sortFlag) {
    var v = [];
    for (var j in obj) {
	if (obj[j].Name === 'info_reserved6') {
	    // swallow this - it is the player's hot slots
	} else {
	    var s = '<li>' + obj[j].Name;
	    if (obj[j].Count !== undefined) {
		s += ' (' + obj[j].Count + ')';
	    }
	    if (obj[j].Enchantments !== undefined) {
		var ench = obj[j].Enchantments;
		s += ' (';
		var i = ench.length;
		for (var k in ench) {
		    s += ench[k].Name;
		    if (--i > 0) {
			s += '; ';
		    }
		}
		s += ')';
	    }
	    s += '</li>';
	    v.push(s);
	}
    }
    // we sort the items, yay
    if (sortFlag) {
	v.sort();
    }
    return v.join('\n');
}


// adapted from: http://openlayers.org/en/v3.9.0/examples/vector-layer.html
// todo - this does not work?
var highlightStyleCache = {};
var featureOverlay = new ol.layer.Vector({
    source: new ol.source.Vector(),
    map: map,
    style: function(feature, resolution) {
	var text = resolution < 5000 ? feature.get('Name') : '';
	if (!highlightStyleCache[text]) {
	    highlightStyleCache[text] = [new ol.style.Style({
		stroke: new ol.style.Stroke({
		    color: '#f00',
		    width: 1
		}),
		fill: new ol.style.Fill({
		    color: 'rgba(255,0,0,0.1)'
		}),
		text: new ol.style.Text({
		    font: '12px Calibri,sans-serif',
		    text: text,
		    fill: new ol.style.Fill({
			color: '#000'
		    }),
		    stroke: new ol.style.Stroke({
			color: '#f00',
			width: 3
		    })
		})
	    })];
	}
	return highlightStyleCache[text];
    }
});

function correctGeoJSONName(name) {
    if (name == 'MobSpawner') { name = 'Mob Spawner'; }
    if (name == 'NetherPortal') { name = 'Nether Portal'; }
    return name;
}

function doFeaturePopover(feature, coordinate) {
    var element = popover.getElement();
    var props = feature.getProperties();

    var name = correctGeoJSONName(props.Name);

    var stitle;
    if (props.Entity !== undefined) {
	stitle = '<div class="mob"><span class="mob_name">' + name + '</span> <span class="mob_id">(id=' + props.id + ')</span></div>\n';
    } else {
	stitle = '<div class="mob"><span class="mob_name">' + name + '</span></div>\n';
    }
    
    var s = '<div>';
    
    for (var i in props) {
	// skip geometry property because it contains circular refs; skip others that are uninteresting
	if (i !== 'geometry' &&
	    i !== 'TileEntity' &&
	    i !== 'Entity' &&
	    i !== 'pairchest' &&
	    i !== 'player' &&
	    i !== 'Name' &&
	    i !== 'id' &&
	    i !== 'Dimension'
	   ) {
	    if (typeof(props[i]) === 'object') {
		if (i === 'Armor') {
		    var armor = props[i];
		    s += 'Armor:<ul>' + doParsedItem(armor, false) + '</ul>';
		}
		else if (i === 'Items') {
		    // items in a chest
		    var items = props[i];
		    s += 'Items:<ul>' + doParsedItem(items, true) + '</ul>';
		}
		else if (i === 'Inventory') {
		    var inventory = props[i];
		    s += 'Inventory:<ul>' + doParsedItem(inventory, true) + '</ul>';
		}
		else if (i === 'ItemInHand') {
		    var itemInHand = props[i];
		    s += 'In Hand:<ul>' + doParsedItem([itemInHand], false) + '</ul>';
		}
		else if (i === 'Item') {
		    var item = props[i];
		    s += 'Item:<ul>' + doParsedItem([item], false) + '</ul>';
		}
		else if (i === 'Sign') {
		    s += '<div class="mycenter">' +
			props[i].Text1 + '<br/>' +
			props[i].Text2 + '<br/>' +
			props[i].Text3 + '<br/>' +
			props[i].Text4 + '<br/>' +
			'</div>';
		}
		else if (i === 'MobSpawner') {
		    s += '' +
			'Name: <b>' + props[i].Name + '</b><br/>' +
			'entityId: <b>' + props[i].entityId + '</b><br/>';
		}
		else {
		    s += '' + i + ': ' + JSON.stringify(props[i], null, 2) + '<br/>';
		}
	    } else {
		s += '' + i + ': <b>' + props[i].toString() + '</b><br/>';
	    }
	}
    }
    s += '</div>';
    popover.setPosition(coordinate);
    $(element).popover({
	'placement': 'auto right',
	'viewport': {selector: '#map', padding: 0},
	'animation': false,
	'trigger': 'click focus',
	'html': true,
	'title': stitle,
	'content': s
    });
    $(element).popover('show');
    
    // todo - disabled because this does not appear to work
    if (false) {
	if (feature !== highlight) {
	    if (highlight) {
		featureOverlay.getSource().removeFeature(highlight);
	    }
	    if (feature) {
		featureOverlay.getSource().addFeature(feature);
	    }
	    highlight = feature;
	}
    }

    return 0;
}

function doFeatureSelect(features, coordinate) {
    var element = popover.getElement();

    var stitle = '<div class="mob"><span class="mob_name">Multiple Items</span></div>\n';

    features.sort(function(a, b) { 
	var ap = a.getProperties();
	var astr = ap.Name + ' @ ' + ap.Pos[0] + ', ' + ap.Pos[1] + ', ' + ap.Pos[2];
	var bp = b.getProperties();
	var bstr = bp.Name + ' @ ' + bp.Pos[0] + ', ' + bp.Pos[1] + ', ' + bp.Pos[2];
	return astr.localeCompare(bstr);
    });
    
    var s = 'Select item:<div class="list-group">';
    for (var i in features) {
	var feature = features[i];
	var props = feature.getProperties();
	// how to do this?
	s += '<a href="#" data-id="' + i + '" class="list-group-item doFeatureHelper">' +
	    correctGeoJSONName(props.Name) +
	    ' @ ' + props.Pos[0] + ', ' + props.Pos[1] + ', ' + props.Pos[2] +
	    '</a>';
    }
    s += '</div>';
    
    popover.setPosition(coordinate);
    $(element).popover({
	'placement': 'auto right',
	'viewport': {selector: '#map', padding: 0},
	'animation': false,
	'trigger': 'click focus',
	'html': true,
	'title': stitle,
	'content': s
    });
    $(element).popover('show');
    // setup click helper for the list of items
    $('.doFeatureHelper').click(function() {
	var id = +$(this).attr('data-id');
	$(element).popover('destroy');
	doFeaturePopover(features[id], coordinate);
    });
}

var highlight;
var displayFeatureInfo = function(evt) {
    var pixel = evt.pixel;
    var coordinate = evt.coordinate;
    var element = popover.getElement();

    $(element).popover('destroy');

    // we get a list in case there are multiple items (e.g. stacked chests)
    var features = [];
    map.forEachFeatureAtPixel(pixel, function(feature, layer) {
	features.push(feature);
    });

    if (features.length > 0) {

	if (features.length === 1) {
	    doFeaturePopover(features[0], coordinate);
	} else {
	    // we need to show a feature select list
	    doFeatureSelect(features, coordinate);
	}
    }
};

// http://stackoverflow.com/questions/14484787/wrap-text-in-javascript
function stringDivider(str, width, spaceReplacer) {
    if (str.length > width) {
	var p = width;
	for (; p > 0 && (str[p] != ' ' && str[p] != '-'); p--) {
	}
	if (p > 0) {
	    var left;
	    if (str.substring(p, p + 1) == '-') {
		left = str.substring(0, p + 1);
	    } else {
		left = str.substring(0, p);
	    }
	    var right = str.substring(p + 1);
	    return left + spaceReplacer + stringDivider(right, width, spaceReplacer);
	}
    }
    return str;
}

// from: http://openlayers.org/en/v3.9.0/examples/vector-labels.html
var getText = function(feature, resolution) {
    var type = 'normal';
    var maxResolution = 2;
    var text = correctGeoJSONName(feature.get('Name'));

    if (true) {
	if (resolution > maxResolution) {
	    text = '';
	} else if (type == 'hide') {
	    text = '';
	} else if (type == 'shorten') {
	    text = text.trunc(12);
	} else if (type == 'wrap') {
	    text = stringDivider(text, 16, '\n');
	}
    }
    
    return text;
};

var createTextStyle = function(feature, resolution) {
    var align = 'left';
    var baseline = 'bottom';
    var size = '14pt';
    var offsetX = 3;
    var offsetY = -3;
    var weight = 'bold';
    var rotation = 0;
    var font = weight + ' ' + size + ' Calibri,sans-serif';
    var fillColor = '#ffffff';
    var outlineColor = '#000000';
    var outlineWidth = 3;

    return new ol.style.Text({
	textAlign: align,
	textBaseline: baseline,
	font: font,
	color: '#ffffff',
	text: getText(feature, resolution),
	fill: new ol.style.Fill({color: fillColor}),
	stroke: new ol.style.Stroke({color: outlineColor, width: outlineWidth}),
	offsetX: offsetX,
	offsetY: offsetY,
	rotation: rotation
    });
};


function setLayerLoadListeners(src, fn) {
    src.on('imageloadstart', function(event) {
	updateLoadEventCount(1);
    });
    src.on('imageloadend', function(event) {
	updateLoadEventCount(-1);
    });
    src.on('imageloaderror', function(event) {
	updateLoadEventCount(-1);
	alert('Image load error.\n' +
	      '\n' +
	      'Could not load file: ' + fn);
    });
}


// originally from: http://openlayers.org/en/v3.10.0/examples/shaded-relief.html
// but that code is actually *quite* insane
// rewritten based on:
//   http://edndoc.esri.com/arcobjects/9.2/net/shared/geoprocessing/spatial_analyst_tools/how_hillshade_works.htm

// todo what does this comment do? (from openlayers version)
// NOCOMPILE


/**
 * Generates a shaded relief image given elevation data.  Uses a 3x3
 * neighborhood for determining slope and aspect.
 * @param {Array.<ImageData>} inputs Array of input images.
 * @param {Object} data Data added in the 'beforeoperations' event.
 * @return {ImageData} Output image.
 */
function shade(inputs, data) {
    try {
	var elevationImage = inputs[0];
	var width = elevationImage.width;
	var height = elevationImage.height;
	var elevationData = elevationImage.data;
	var shadeData = new Uint8ClampedArray(elevationData.length);
	var maxX = width - 1;
	var maxY = height - 1;
	var twoPi = 2 * Math.PI;
	var halfPi = Math.PI / 2;

	// (2)  Zenith_deg = 90 - Altitude
	// (3)  Zenith_rad = Zenith_deg * pi / 180.0
	var zenithRad = (90 - data.sunEl) * Math.PI / 180;

	// (4)  Azimuth_math = 360.0 - Azimuth + 90
	var azimuthMath = 360 - data.sunAz + 90;
	// (5)  if Azimth_math >= 360.0 : Azimuth_math = Azimuth_math - 360.0
	if (azimuthMath >= 360.0) {
	    azimuthMath = azimuthMath - 360.0;
	}
	// (6)  Azimuth_rad = Azimuth_math *  pi / 180.0
	var azimuthRad = azimuthMath * Math.PI / 180.0;

	var cosZenithRad = Math.cos(zenithRad);
	var sinZenithRad = Math.sin(zenithRad);

	// todo - since we need to multiply x2 to expand 0..127 to 0..255 we instead halve this (would be 8)
	var dp = data.resolution * 4.0;  // data.resolution * 8; // todo - not totally sure about the use of resolution here

	// notes: negative values simply reverse the sun azimuth; the range of interesting values is fairly narrow - somewhere on (0.001..0.8)
	var zFactor = (data.vert / 10.0) - 0.075;

	var x0, x1, x2, 
	    y0, y1, y2, 
	    offset,
	    z0, z2, 
	    dzdx, dzdy, 
	    slopeRad, aspectRad, hillshade, fhillshade;

	/* 
	   our 3x3 grid:
	   a b c
	   d e f
	   g h i
	   
	   y0 is row above curr
	   y1 is curr
	   y2 is row below curr

	   x0 is col to left of curr
	   x1 is curr
	   x2 is col to right of curr
	*/

	for (y1 = 0; y1 <= maxY; ++y1) {
	    y0 = (y1 === 0) ? 0 : (y1 - 1);
	    y2 = (y1 === maxY) ? maxY : (y1 + 1);

	    for (x1 = 0; x1 <= maxX; ++x1) {
		x0 = (x1 === 0) ? 0 : (x1 - 1);
		x2 = (x1 === maxX) ? maxX : (x1 + 1);

		// z0 = a + 2d + g
		z0 = 
		    elevationData[(y0 * width + x0) * 4] + 
		    elevationData[(y1 * width + x0) * 4] * 2.0 + 
		    elevationData[(y2 * width + x0) * 4];

		// z2 = c + 2f + i
		z2 = 
		    elevationData[(y0 * width + x2) * 4] + 
		    elevationData[(y1 * width + x2) * 4] * 2.0 + 
		    elevationData[(y2 * width + x2) * 4];
		
		// (7)  [dz/dx] = ((c + 2f + i) - (a + 2d + g)) / (8 * cellsize)
		dzdx = (z2 - z0) / dp;


		// z0 = a + 2b + c
		z0 = 
		    elevationData[(y0 * width + x0) * 4] + 
		    elevationData[(y0 * width + x1) * 4] * 2.0 + 
		    elevationData[(y0 * width + x2) * 4];

		// z2 = g + 2h + i
		z2 = 
		    elevationData[(y2 * width + x0) * 4] + 
		    elevationData[(y2 * width + x1) * 4] * 2.0 + 
		    elevationData[(y2 * width + x2) * 4];

		// (8)  [dz/dy] = ((g + 2h + i) - (a + 2b + c))  / (8 * cellsize)
		dzdy = (z2 - z0) / dp;

		// (9)  Slope_rad = ATAN (z_factor * sqrt ([dz/dx]2 + [dz/dy]2)) 
		slopeRad = Math.atan(zFactor * Math.sqrt(dzdx * dzdx + dzdy * dzdy));

		if (dzdx !== 0.0) { 
		    aspectRad = Math.atan2(dzdy, -dzdx);

		    if (aspectRad < 0) {
			aspectRad = twoPi + aspectRad;
		    }
		}
		else {
		    if (dzdy > 0.0) {
			aspectRad = halfPi;
		    } 
		    else if (dzdy < 0.0) {
			aspectRad = twoPi - halfPi;
		    }
		    else {
			// aspectRad is fine
			aspectRad = 0.0; // todo - this is my guess; algo notes are ambiguous
		    }
		}
		
		// (1)  Hillshade = 255.0 * ((cos(Zenith_rad) * cos(Slope_rad)) + 
		//        (sin(Zenith_rad) * sin(Slope_rad) * cos(Azimuth_rad - Aspect_rad)))
		// Note that if the calculation of Hillshade value is < 0, the cell value will be = 0.

		fhillshade = 255.0 * ((cosZenithRad * Math.cos(slopeRad)) + (sinZenithRad * Math.sin(slopeRad) * Math.cos(azimuthRad - aspectRad)));

		if (fhillshade < 0.0) {
		    hillshade = 0;
		} else {
		    hillshade = Math.round(fhillshade);
		}

		offset = (y1 * width + x1) * 4;
		shadeData[offset] =
		    shadeData[offset + 1] =
		    shadeData[offset + 2] = hillshade;
		// note: reduce the opacity for brighter parts; idea is to reduce haziness
		shadeData[offset + 3] = 255 - (hillshade / 2);
	    }
	}

	return new ImageData(shadeData, width, height);
    } catch (e) {
	// we are probably failing because of CORS
	alert('Error accessing map pixels.  Disabling elevation overlay.\n\n' +
	      'Error: ' + e.toString() + '\n\n' +
	      globalCORSWarning);
	map.removeLayer(layerElevation);
    }
    // todobig todohere - how to catch CORS issue here?
}

var srcElevation = null, rasterElevation = null, layerElevation = null;

function doShadedRelief(enableFlag) {
    try {
	if (enableFlag) {
	    var fn = dimensionInfo[globalDimensionId].fnLayerHeightGrayscale;
	    if (fn === undefined || fn.length <= 1) {
		alert('Data for elevation image is not available -- see README.md and re-run mcpe_viz\n' +
		      '\n' +
		      'Hint: You need to run mcpe_viz with --html-most (or --html-all)');
		return -1;
	    }
	    var doInitFlag = false;
	    if (srcElevation === null) {
		doInitFlag = true;
		srcElevation = new ol.source.ImageStatic({
		    url: fn,
		    //crossOrigin: 'anonymous',
		    projection: projection,
		    imageSize: [dimensionInfo[globalDimensionId].worldWidth, dimensionInfo[globalDimensionId].worldHeight],
		    // 'Extent of the image in map coordinates. This is the [left, bottom, right, top] map coordinates of your image.'
		    imageExtent: extent
		});

		setLayerLoadListeners(srcElevation, fn);

		rasterElevation = new ol.source.Raster({
		    sources: [srcElevation],
		    operationType: 'image',
		    operation: shade
		});

		layerElevation = new ol.layer.Image({
		    opacity: 0.3,
		    source: rasterElevation
		});
	    }

	    map.addLayer(layerElevation);

	    if (doInitFlag) {
		var controlIds = ['vert', 'sunEl', 'sunAz', 'shadeOpacity'];
		var controls = {};
		controlIds.forEach(function(id) {
		    var control = document.getElementById(id);
		    var output = document.getElementById(id + 'Out');
		    // todo - this does NOT update the text fields on firefox - why?
		    control.addEventListener('input', function() {
			output.innerText = control.value;
			rasterElevation.changed();
		    });
		    output.innerText = control.value;
		    controls[id] = control;
		});

		rasterElevation.on('beforeoperations', function(event) {
		    // the event.data object will be passed to operations
		    var data = event.data;
		    data.resolution = event.resolution;
		    for (var id in controls) {
			data[id] = Number(controls[id].value);
		    }
		});
	    }
	} else {
	    if (layerElevation !== null) {
		map.removeLayer(layerElevation);
	    }
	}
    } catch (e) {
	alert('Error accessing map pixels.\n\n' +
	      'Error: ' + e.toString() + '\n\n' +
	      globalCORSWarning);
    }
    // todobig todohere - how to catch CORS issue here?
    return 0;
}


function makeChunkGrid(inputs, data) {
    var srcImage = inputs[0];
    var width = srcImage.width;
    var height = srcImage.height;
    var srcData = srcImage.data;
    var gridData = new Uint8ClampedArray(srcData.length);
    var dx = data.resolution;
    var dy = data.resolution;
    
    //console.log('makeChunkGrid w=' + width + ' h=' + height + ' dx='+dx+' dy='+dy);

    // todo - so fiddly.  it's still off a bit (not 100% locked to src pixels)
    
    var cy = data.extent[3];
    for (var pixelY = 0; pixelY < height; ++pixelY, cy -= dy) {
	var icy = Math.round(((data.worldHeight - 1) - cy) + data.globalOffsetY);
	var chunkY = Math.round(icy / 16);
	var cx = data.extent[0];
	for (var pixelX = 0; pixelX < width; ++pixelX, cx += dx) {
	    var offset = (pixelY * width + pixelX) * 4;

	    var icx = Math.round(cx + data.globalOffsetX);
	    var chunkX = Math.round(icx / 16);
	    if (((icx % 16) === 0) || ((icy % 16) === 0)) {
		if (chunkX === 0 && chunkY === 0) {
		    gridData[offset] = 255;
		    gridData[offset + 1] = 0;
		    gridData[offset + 2] = 0;
		    gridData[offset + 3] = 128; 
		} else {
		    gridData[offset] = 255;
		    gridData[offset + 1] = 255;
		    gridData[offset + 2] = 255;
		    gridData[offset + 3] = 128;
		}
	    } else {
		gridData[offset] = 
		    gridData[offset + 1] = 
		    gridData[offset + 2] = 
		    gridData[offset + 3] = 0;
	    }
	}
    }
    return new ImageData(gridData, width, height);
}

var rasterChunkGrid = null, layerChunkGrid = null;

function doChunkGrid(enableFlag) {
    if (enableFlag) {
	if (srcLayerMain === null) {
	    alert('Werid.  Main layer source is null.  Cannot proceed.');
	    return -1;
	}
	var doInitFlag = false;
	if (rasterChunkGrid === null) {
	    doInitFlag = true;

	    rasterChunkGrid = new ol.source.Raster({
		sources: [srcLayerMain],
		operationType: 'image',
		operation: makeChunkGrid
	    });

	    layerChunkGrid = new ol.layer.Image({
		opacity: 0.4,
		source: rasterChunkGrid
	    });
	}

	map.addLayer(layerChunkGrid);

	if (doInitFlag) {
	    rasterChunkGrid.on('beforeoperations', function(event) {
		// the event.data object will be passed to operations
		var data = event.data;
		data.resolution = event.resolution;
		data.extent = event.extent;
		data.worldWidth = dimensionInfo[globalDimensionId].worldWidth;
		data.worldHeight = dimensionInfo[globalDimensionId].worldHeight;
		data.globalOffsetX = dimensionInfo[globalDimensionId].globalOffsetX;
		data.globalOffsetY = dimensionInfo[globalDimensionId].globalOffsetY;
		//console.log('rasterChunkGrid resolution=' + event.resolution + ' extent=' + event.extent);
	    });
	}
    } else {
	if (layerChunkGrid !== null) {
	    map.removeLayer(layerChunkGrid);
	}
    }
    return 0;
}

function setLayer(fn, extraHelp) {
    if (fn.length <= 1) {
	if ( extraHelp === undefined ) {
	    extraHelp = '';
	} else {
	    extraHelp = '\n\nHint: ' + extraHelp;
	}
	alert('That image is not available -- see README.md and re-run mcpe_viz.' + extraHelp);
	return -1;
    }
    
    // todo - attribution is small and weird in map - why?
    srcLayerMain = new ol.source.ImageStatic({
	attributions: [
	    new ol.Attribution({
		html: 'Created by <a href="https://github.com/Plethora777/mcpe_viz" target="_blank">mcpe_viz</a>'
	    })
	],
	url: fn,
	//crossOrigin: 'anonymous',
	projection: projection,
	imageSize: [dimensionInfo[globalDimensionId].worldWidth, dimensionInfo[globalDimensionId].worldHeight],
	// 'Extent of the image in map coordinates. This is the [left, bottom, right, top] map coordinates of your image.'
	imageExtent: extent
    });

    setLayerLoadListeners(srcLayerMain, fn);
    
    if (layerMain === null) {
	layerMain = new ol.layer.Image({source: srcLayerMain});
	map.addLayer(layerMain);

	// get the pixel position with every move
	$(map.getViewport()).on('mousemove', function(evt) {
	    globalMousePosition = map.getEventPixel(evt.originalEvent);
	    // todo - is this too expensive? is there a better way?
	    map.render();
	}).on('mouseout', function() {
	    globalMousePosition = null;
	    map.render();
	});
	
	layerMain.on('postcompose', function(event) {
	    try {
		var ctx = event.context;
		var pixelRatio = event.frameState.pixelRatio;
		pixelDataName = '';
		if (globalMousePosition && 
		    ((globalLayerMode === 0 && (globalLayerId === 0 || globalLayerId === 1)) || globalLayerMode !== 0)) {
		    // todo - this appears to be slightly off at times (e.g. block does not change crisply at src pixel boundaries)
		    var x = globalMousePosition[0] * pixelRatio;
		    var y = globalMousePosition[1] * pixelRatio;
		    var pre = '';
		    pixelData = ctx.getImageData(x, y, 1, 1).data;
		    var cval = (pixelData[0] << 16) | (pixelData[1] << 8) | pixelData[2];
		    if (globalLayerMode === 0 && globalLayerId === 1) {
			pre = 'Biome';
			pixelDataName = biomeColorLUT['' + cval];
		    } else {
			pre = 'Block';
			pixelDataName = blockColorLUT['' + cval];
		    }
		    if (pixelDataName === undefined || pixelDataName === '') {
			if (pixelData[0] === 0 && pixelData[1] === 0 && pixelData[2] === 0) {
			    pixelDataName = '(<i>Here be Monsters</i> -- unexplored chunk)';
			} else {
			    pixelDataName = '<span class="lgray">' + pre + '</span> ' + 'Unknown RGB: ' + pixelData[0] + ' ' + pixelData[1] + ' ' + pixelData[2] + ' (' + cval + ')';
			}
		    } else {
			pixelDataName = '<span class="lgray">' + pre + '</span> ' + pixelDataName;
		    }
		}
	    } catch (e) {
		pixelDataName = '<i>Browser will not let us access map pixels - See README.md</i>';
		if ( ! globalCORSWarningFlag ) {
		    alert('Error accessing map pixels.\n\n' +
			  'Error: ' + e.toString() + '\n\n' +
			  globalCORSWarning);
		    globalCORSWarningFlag = true;
		}
	    }
	});
	
	var bindLayerListeners = function(layer) {
	    layer.on('precompose', setCanvasSmoothingMode);
	    layer.on('postcompose', resetCanvasSmoothingMode);
	};
	bindLayerListeners(layerMain);
    } else {
	layerMain.setSource(srcLayerMain);
    }
    return 0;
}


function setLayerById(id) {
    var extraHelp = 'You need to run mcpe_viz with --html-most (or --html-all)';
    if (0) {
    } else if (id === 1) {
	if (setLayer(dimensionInfo[globalDimensionId].fnLayerBiome, extraHelp) === 0) {
	    globalLayerMode = 0; globalLayerId = 1;
	    $('#imageSelectName').html('Biome');
	}
    } else if (id === 2) {
	if (setLayer(dimensionInfo[globalDimensionId].fnLayerHeight, extraHelp) === 0) {
	    globalLayerMode = 0; globalLayerId = 2;
	    $('#imageSelectName').html('Height');
	}
    } else if (id === 3) {
	if (setLayer(dimensionInfo[globalDimensionId].fnLayerHeightGrayscale, extraHelp) === 0) {
	    globalLayerMode = 0; globalLayerId = 3;
	    $('#imageSelectName').html('Height (Grayscale)');
	}
    } else if (id === 4) {
	if (setLayer(dimensionInfo[globalDimensionId].fnLayerBlockLight, extraHelp) === 0) {
	    globalLayerMode = 0; globalLayerId = 4;
	    $('#imageSelectName').html('Block Light');
	}
    } else if (id === 5) {
	if (setLayer(dimensionInfo[globalDimensionId].fnLayerGrass, extraHelp) === 0) {
	    globalLayerMode = 0; globalLayerId = 5;
	    $('#imageSelectName').html('Grass Color');
	}
    } else {
	// default is overview map
	if (setLayer(dimensionInfo[globalDimensionId].fnLayerTop, '') === 0) {
	    globalLayerMode = 0; globalLayerId = 0;
	    $('#imageSelectName').html('Overview');
	}
    }
}


function initDimension() {
    // Map views always need a projection.  Here we just want to map image
    // coordinates directly to map coordinates, so we create a projection that uses
    // the image extent in pixels.
    dimensionInfo[globalDimensionId].worldWidth =
	dimensionInfo[globalDimensionId].maxWorldX - dimensionInfo[globalDimensionId].minWorldX + 1;
    dimensionInfo[globalDimensionId].worldHeight =
	dimensionInfo[globalDimensionId].maxWorldY - dimensionInfo[globalDimensionId].minWorldY + 1;

    extent = [0, 0,
	      dimensionInfo[globalDimensionId].worldWidth - 1,
	      dimensionInfo[globalDimensionId].worldHeight - 1];

    dimensionInfo[globalDimensionId].globalOffsetX = dimensionInfo[globalDimensionId].minWorldX;
    dimensionInfo[globalDimensionId].globalOffsetY = dimensionInfo[globalDimensionId].minWorldY;

    console.log('World bounds: dimId=' + globalDimensionId +
		' w=' + dimensionInfo[globalDimensionId].worldWidth +
		' h=' + dimensionInfo[globalDimensionId].worldHeight +
		' offx=' + dimensionInfo[globalDimensionId].globalOffsetX + 
		' offy=' + dimensionInfo[globalDimensionId].globalOffsetY
	       );
    
    projection = new ol.proj.Projection({
	code: 'mcpe_viz-image',
	units: 'm',
	extent: extent,
	getPointResolution: function(resolution, coordinate) {
	    return resolution;
	}
    });

    if (mousePositionControl === null) {
	mousePositionControl = new ol.control.MousePosition({
	    coordinateFormat: coordinateFormatFunction,
	    projection: projection,
	    // comment the following two lines to have the mouse position be placed within the map.
	    // className: 'custom-mouse-position',
	    //target: document.getElementById('mouse-position'),
	    undefinedHTML: '&nbsp;'
	});
    } else {
	mousePositionControl.setProjection(projection);
    }

    /*
      var attribution = new ol.control.Attribution({
      collapsed: false,
      collapsible: false
      //target: '_blank'
      });		
    */
    
    if (map === null) {
	map = new ol.Map({
	    controls: ol.control.defaults({
		attribution: true,
		attributionOptions: { collapsed: false, collapsible: false, target: '_blank' }
	    })
		.extend([
		    new ol.control.ZoomToExtent(),
		    new ol.control.ScaleLine(),
		    new ol.control.FullScreen(),
		    mousePositionControl
		]),
	    // pixelRatio: 1, // todo - need this?
	    target: 'map',
	    view: new ol.View({
		projection: projection,
		center: [dimensionInfo[globalDimensionId].playerPosX, dimensionInfo[globalDimensionId].playerPosY],
		resolution: 1
	    })
	});
    } else {
	var view = new ol.View({
	    projection: projection,
	    center: [dimensionInfo[globalDimensionId].playerPosX, dimensionInfo[globalDimensionId].playerPosY],
	    resolution: 1
	});
	map.setView(view);
    }

    // finally load the proper layer
    if (globalLayerMode === 0) {
	return setLayerById(globalLayerId);
    } else {
	return layerGoto(layerRawIndex);
    }
}

function setDimensionById(id) {
    var prevDID = globalDimensionId;
    if (0) {
    }
    else if (id === 1) {
	globalDimensionId = id;
	$('#dimensionSelectName').html('Nether');
    }
    else {
	// default to overworld
	globalDimensionId = id;
	$('#dimensionSelectName').html('Overworld');
    }

    if (prevDID !== globalDimensionId) {
	initDimension();
    }
}


var createPointStyleFunction = function() {
    return function(feature, resolution) {
	var style;
	var entity = feature.get('Entity');
	var tileEntity = feature.get('TileEntity');
	var did = feature.get('Dimension');

	// hack for pre-0.12 worlds
	if (did === undefined) {
	    did = 0;
	} else {
	    did = +did;
	}
	
	if (entity !== undefined) {
	    if (did === globalDimensionId) {
		var id = +feature.get('id');
		if (listEntityToggle[id] !== undefined) {
		    if (listEntityToggle[id]) { 
			style = new ol.style.Style({
			    image: new ol.style.Circle({
				radius: 4,
				fill: new ol.style.Fill({color: 'rgba(255, 255, 255, 1.0)'}),
				stroke: new ol.style.Stroke({color: 'rgba(0, 0, 0, 1.0)', width: 2})
			    }),
			    text: createTextStyle(feature, resolution)
			});
			return [style];
		    }
		}
	    }
	}
	else if (tileEntity !== undefined) {
	    if (did === globalDimensionId) {
		var Name = feature.get('Name');
		if (listTileEntityToggle[Name] !== undefined) {
		    if (listTileEntityToggle[Name]) { 
			style = new ol.style.Style({
			    image: new ol.style.Circle({
				radius: 4,
				fill: new ol.style.Fill({color: 'rgba(255, 255, 255, 1.0)'}),
				stroke: new ol.style.Stroke({color: 'rgba(0, 0, 0, 1.0)', width: 2})
			    }),
			    text: createTextStyle(feature, resolution)
			});
			return [style];
		    }
		}
	    }
	}
	else {
	    console.log('Weird.  Found a GeoJSON item that is not an entity or a tileEntitiy');
	}
	return null;
    };
};

var vectorPoints = null;

function loadVectors() {
    if (vectorPoints !== null) {
	map.removeLayer(vectorPoints);
    }

    try {
	var src;
	if ( loadGeoJSONFlag ) { 
	    src = new ol.source.Vector({
		url: dimensionInfo[globalDimensionId].fnGeoJSON,
		//crossOrigin: 'anonymous',
		format: new ol.format.GeoJSON()
	    });
	    updateLoadEventCount(1);
	} else {
	    // we are loading the geojson directly to work-around silly chrome (et al) CORS issue
	    // adapted from ol/featureloader.js
	    var format = new ol.format.GeoJSON();
	    var features = format.readFeatures(geojson, {featureProjection: projection});
	    src = new ol.source.Vector({
		features: features
	    });
	}
	
	var listenerKey = src.on('change', function(e) {
	    if (src.getState() == 'ready') {
		updateLoadEventCount(-1);
		ol.Observable.unByKey(listenerKey);
	    }
	    else if (src.getState() == 'error') {
		updateLoadEventCount(-1);
		ol.Observable.unByKey(listenerKey);
		alert('Image load error.\n' +
		      '\n' +
		      'Could not load file: ' + src.url + '\n' +
		      globalCORSWarning);
	    }
	});
	
	vectorPoints = new ol.layer.Vector({
	    source: src,
	    style: createPointStyleFunction()
	});
	
	map.addLayer(vectorPoints);
    } catch (e) {
	updateLoadEventCount(-1);
	alert('Vector load error.\n' +
	      '\n' +
	      'Error: ' + e.toString() + '\n' +
	      '\n' +
	      globalCORSWarning);
    } 
    // todobig todohere - how to catch CORS issue here?
}


function entityToggle(id) {
    id = +id;
    if (vectorPoints === null) {
	loadVectors();
    }
    if (listEntityToggle[id] === undefined) {
	listEntityToggle[id] = true;
    } else {
	listEntityToggle[id] = !listEntityToggle[id];
    }
    vectorPoints.changed();
}

function tileEntityToggle(name) {
    if (vectorPoints === null) {
	loadVectors();
    }
    if (listTileEntityToggle[name] === undefined) {
	listTileEntityToggle[name] = true;
    } else {
	listTileEntityToggle[name] = !listTileEntityToggle[name];
    }
    vectorPoints.changed();
}

function layerMove(delta) {
    //this_.getMap().getView().setRotation(0);
    layerRawIndex += delta;
    if (layerRawIndex < 0) { layerRawIndex = 0; }
    if (layerRawIndex > 127) { layerRawIndex = 127; }
    layerGoto(layerRawIndex);
}

function layerGoto(layer) {
    if (layer < 0) { layer = 0; }
    if (layer > 127) { layer = 127; }
    if (setLayer(dimensionInfo[globalDimensionId].listLayers[layer], 'You need to run mcpe_viz with --html-all') === 0) {
	globalLayerMode = 1;
	layerRawIndex = layer;
	$('#layerNumber').html('' + layer);
    }
}


// todo - this is still not quite right
var coordinateFormatFunction = function(coordinate) {
    var cx = coordinate[0] + dimensionInfo[globalDimensionId].globalOffsetX;
    var cy = ((dimensionInfo[globalDimensionId].worldHeight - 1) - coordinate[1]) + dimensionInfo[globalDimensionId].globalOffsetY;
    var ix = coordinate[0];
    var iy = (dimensionInfo[globalDimensionId].worldHeight - 1) - coordinate[1];
    var prec = 1;
    var s = '<span class="lgray">World</span> ' + cx.toFixed(prec) + ' ' + cy.toFixed(prec) + ' <span class="lgray">Image</span> ' + ix.toFixed(prec) + ' ' + iy.toFixed(prec);
    if (pixelDataName.length > 0) {
	s += '<br/>' + pixelDataName;
    }
    return s;
};

// adapted from: http://stackoverflow.com/questions/12887506/cannot-set-maps-div-height-for-openlayers-map
var fixContentHeight = function() {
    var viewHeight = $(window).height();
    var navbar = $('div[data-role="navbar"]:visible:visible');
    // todo - this is not quite right, off by approx 7 pixels - why?
    var newMapH = viewHeight - navbar.outerHeight();
    var curMapSize = map.getSize();
    curMapSize[1] = newMapH;
    map.setSize(curMapSize);
};

$(function() {

    // add the main layer
    setDimensionById(0);

    popover = new ol.Overlay({
	element: document.getElementById('popover'),
	autoPan: true,
	autoPanAnimation: {
	    duration: 100
	}
    });
    // todo - do we need to do this when we create/update popover? hmmm
    map.addOverlay(popover);

    // todo - refine overview map cfg and add this back?
    if (false) {
	var omap = new ol.control.OverviewMap({
	    layers: [layerMain]
	});
	map.addControl(omap);
    }
    
    map.on('singleclick', function(evt) {
	displayFeatureInfo(evt);
    });

    $('.dimensionSelect').click(function() {
	var id = +$(this).attr('data-id');
	setDimensionById(id);
    });
    
    
    $('.layerGoto').click(function() {
	var id = +$(this).attr('data-id');
	layerGoto(id);
    });
    
    $('#layerPrev').click(function() { layerMove(-1); });
    $('#layerNext').click(function() { layerMove(1); });
    
    $('.imageSelect').click(function() {
	var id = +$(this).attr('data-id');
	setLayerById(id);
    });
    
    $('.entityToggleAddAll').click(function() {
	var dtype = $(this).attr('data-type');
	if ( vectorPoints === null ) {
	    loadVectors();
	}
	$('.entityToggle').each(function(index) {
	    if ($(this).attr('data-type') === dtype) {
		var id = +$(this).attr('data-id');
		listEntityToggle[id] = true;
		$(this).parent().addClass('active');
	    }
	});
	vectorPoints.changed();
    });
    $('.entityToggleRemoveAll').click(function() {
	listEntityToggle = [];
	if (vectorPoints !== null) { 
	    vectorPoints.changed();
	}
	$('.entityToggle').parent().removeClass('active');
    });
    $('.entityToggle').click(function() {
	var id = $(this).attr('data-id');
	entityToggle(id);
	if (listEntityToggle[id]) {
	    $('.entityToggle[data-id=' + id + ']').parent().addClass('active');
	} else {
	    $('.entityToggle[data-id=' + id + ']').parent().removeClass('active');
	}
    });

    $('.tileEntityToggleAddAll').click(function() {
	if ( vectorPoints === null ) {
	    loadVectors();
	}
	$('.tileEntityToggle').each(function(index) {
	    listTileEntityToggle[$(this).attr('data-id')] = true;
	    $(this).parent().addClass('active');
	});
	vectorPoints.changed();
    });
    $('.tileEntityToggleRemoveAll').click(function() {
	listTileEntityToggle = [];
	if (vectorPoints !== null) { 
	    vectorPoints.changed();
	}
	$('.tileEntityToggle').parent().removeClass('active');
    });
    $('.tileEntityToggle').click(function() {
	var id = $(this).attr('data-id');
	tileEntityToggle(id);
	if (listTileEntityToggle[id]) {
	    $('.tileEntityToggle[data-id=' + id + ']').parent().addClass('active');
	} else {
	    $('.tileEntityToggle[data-id=' + id + ']').parent().removeClass('active');
	}
    });

    $('#gridToggle').click(function() {
	if ($('#gridToggle').parent().hasClass('active')) {
	    $('#gridToggle').parent().removeClass('active');
	    doChunkGrid(false);
	} else {
	    $('#gridToggle').parent().addClass('active');
	    doChunkGrid(true);
	}
    });
    
    $('#elevationToggle').click(function() {
	if ( globalCORSWarningFlag ) {
	    alert('Error accessing map pixels.  We cannot enable the elevation overlay.\n\n' +
		  globalCORSWarning);
	    return;
	}
	if ($('#elevationToggle').parent().hasClass('active')) {
	    $('#elevationToggle').parent().removeClass('active');
	    doShadedRelief(false);
	} else {
	    $('#elevationToggle').parent().addClass('active');
	    doShadedRelief(true);
	}
    });
    $('#elevationReset').click(function() {
	$('#vert').val($('#vert').attr('data-default'));
	$('#sunEl').val($('#sunEl').attr('data-default'));
	$('#sunAz').val($('#sunAz').attr('data-default'));
	$('#shadeOpacity').val($('#shadeOpacity').attr('data-default'));
	if (rasterElevation !== null) {
	    rasterElevation.changed();
	}
    });

    $('#shadeOpacity').change(function() {
	if (layerElevation !== null) {
	    layerElevation.setOpacity( $('#shadeOpacity').val() / 100.0 );
	}
    });
    
    // put the world info
    $('#worldInfo').html(
	'<span class="badge mytooltip" title="World Name">' + worldName + '</span>' +
	    '<span class="label mytooltip" title="Imagery Creation Date">' + creationTime + '</span>'
    );

    // setup tooltips
    $('.mytooltip').tooltip({
	// this helps w/ btn groups
	trigger: 'hover',
	container: 'body'
    });
    
    // fix map size
    window.addEventListener('resize', fixContentHeight);
    fixContentHeight();
});
