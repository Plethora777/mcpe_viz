/*
  mcpe_viz openlayers viewer
  by Plethora777 - 2015.10.08

  todo

  * online help/intro text -- bootstrap tour? (is there a CDN for bstour?)

  * measuring tool

  * drawing tools on map -- especially circles (show diameter) (for mob systems)

  * features
  -- hover over pixel gives info: (e.g. "Jungle Biome - Watermelon @ (X,Z,Y)")


  * toggle btwn overworld / nether - show player est. spot in nether even if they are in overworld

  * store biome info in a vector file; use that to display tooltips

  * how to get navbar + popovers in full screen?

  */


/*
  todo - interesting openlayers examples:

  shaded relief -- could try this with height map?
  http://openlayers.org/en/v3.9.0/examples/shaded-relief.html

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
var pixelData = null, pixelDataName = "";

var layerRawIndex = 63;
var layerMain = null;

var mousePositionControl = null;

var globalDimensionId = -1;
var globalLayerMode = 0, globalLayerId = 0;

var listEntityToggle = [];
var listTileEntityToggle = [];


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


var loadEventCount=0;
function updateLoadEventCount(delta) {
    loadEventCount += delta;

    if ( loadEventCount > 0 ) {
        var pos = $( "#map" ).offset();
        var x1 = pos.left + 70;
        var y1 = pos.top + 20;
        $( "#throbber" )
        //.position({ my: "left top", at: "left top", of: "#map" })
            .css( { position: 'absolute', left: x1, top: y1 } )
            .show()
        ;
	
        var a = [];
        if ( loadEventCount > 0 ) {
	    a.push("Layers remaining: "+loadEventCount);
	}
        $( "#throbber-msg" )
            .html(a.join('; '))
        ;
    } else {
        $( "#throbber" ).hide();
    }
}

function doParsedItem(obj,sortFlag) {
    var v = [];
    for ( var j in obj ) {
	if ( obj[j].Name === "info_reserved6" ) {
	    // swallow this - it is the player's hot slots
	} else {
	    var s = "<li>" + obj[j].Name;
	    if ( obj[j].Count !== undefined ) {
		s+= " (" + obj[j].Count + ")";
	    }
	    if ( obj[j].Enchantments !== undefined ) {
		var ench = obj[j].Enchantments;
		s += " (";
		var i = ench.length;
		for ( var k in ench ) {
		    s += ench[k].Name;
		    if ( --i > 0 ) {
			s+="; ";
		    }
		}
		s += ")";
	    }
	    s += "</li>";
	    v.push(s);
	}
    }
    // we sort the items, yay
    if ( sortFlag ) {
	v.sort();
    }
    return v.join("\n");
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

function doFeaturePopover(feature,coordinate) {
    var element = popover.getElement();
    var props = feature.getProperties();

    var name = props.Name;
    if ( name === "MobSpawner" ) { name="Mob Spawner"; }
    if ( name === "NetherPortal" ) { name="Nether Portal"; }
    
    var stitle;
    if ( props.Entity !== undefined ) {
	stitle = "<div class=\"mob\"><span class=\"mob_name\">" + name + "</span> <span class=\"mob_id\">(id=" + props.id + ")</span></div>\n";
    } else {
	stitle = "<div class=\"mob\"><span class=\"mob_name\">" + name + "</span></div>\n";
    }
    
    var s = "<div>";
    
    for ( var i in props ) {
	// skip geometry property because it contains circular refs; skip others that are uninteresting
	if ( i !== "geometry" &&
	     i !== "TileEntity" &&
	     i !== "Entity" &&
	     i !== "pairchest" &&
	     i !== "player" &&
	     i !== "Name" &&
	     i !== "id" &&
	     i !== "Dimension"
	   ) {
	    if ( typeof(props[i]) === 'object' ) {
		if ( i === "Armor" ) {
		    var armor = props[i];
		    s += "Armor:<ul>" + doParsedItem(armor,false) + "</ul>";
		}
		else if ( i === "Items" ) {
		    // items in a chest
		    var items = props[i];
		    s += "Items:<ul>" + doParsedItem(items,true) + "</ul>";
		}
		else if ( i === "Inventory" ) {
		    var inventory = props[i];
		    s += "Inventory:<ul>" + doParsedItem(inventory,true) + "</ul>";
		}
		else if ( i === "ItemInHand" ) {
		    var itemInHand = props[i];
		    s += "In Hand:<ul>" + doParsedItem([itemInHand],false) + "</ul>";
		}
		else if ( i === "Item" ) {
		    var item = props[i];
		    s += "Item:<ul>" + doParsedItem([item],false) + "</ul>";
		}
		else if ( i === "Sign" ) {
		    s += "<div class=\"mycenter\">"
			+ props[i].Text1 + "<br/>"
			+ props[i].Text2 + "<br/>"
			+ props[i].Text3 + "<br/>"
			+ props[i].Text4 + "<br/>"
			+ "</div>"
		    ;
		}
		else if ( i === "MobSpawner" ) {
		    s += ""
			+ "Name: <b>" + props[i].Name + "</b><br/>"
			+ "entityId: <b>" + props[i].entityId + "</b><br/>"
		    ;
		}
		else {
		    s += "" + i + ": " + JSON.stringify(props[i], null, 2) + "<br/>";
		}
	    } else {
		s += "" + i + ": <b>" + props[i].toString() + "</b><br/>";
	    }
	}
    }
    s += "</div>";
    popover.setPosition(coordinate);
    $(element).popover({
	'placement': 'auto right',
	'viewport': {selector: "#map", padding: 0},
	'animation': false,
	'trigger': "click focus",
	'html': true,
	'title': stitle,
	'content': s
    });
    $(element).popover('show');
    
    // todo - disabled because this does not appear to work
    if ( false ) {
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

    var stitle = "<div class=\"mob\"><span class=\"mob_name\">Multiple Items</span></div>\n";

    features.sort( function(a,b) { 
	var ap = a.getProperties();
	var astr = ap.Name + " @ (" + ap.Pos[0] +", "+ ap.Pos[1] +", "+ ap.Pos[2] + ")";
	var bp = b.getProperties();
	var bstr = bp.Name + " @ (" + bp.Pos[0] +", "+ bp.Pos[1] +", "+ bp.Pos[2] + ")";
	return astr.localeCompare(bstr);
    } );
    
    var s = 'Select item:<div class="list-group">';
    for ( var i in features ) {
	var feature = features[i];
	var props = feature.getProperties();
	// how to do this?
	s += '<a href="#" data-id="'+i+'" class="list-group-item doFeatureHelper">'
	    + props.Name
	    + ' @ ' + props.Pos[0] +', '+ props.Pos[1] +', '+ props.Pos[2] + ')'
	    + '</a>'
	;
    }
    s+= "</div>";
    
    popover.setPosition(coordinate);
    $(element).popover({
	'placement': 'auto right',
	'viewport': {selector: "#map", padding: 0},
	'animation': false,
	'trigger': "click focus",
	'html': true,
	'title': stitle,
	'content': s
    });
    $(element).popover('show');
    // setup click helper for the list of items
    $(".doFeatureHelper").click( function() {
	var id = +$(this).attr("data-id");
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

    if ( features.length > 0 ) {

	if ( features.length === 1 )  {
	    doFeaturePopover(features[0], coordinate);
	} else {
	    // we need to show a feature select list
	    doFeatureSelect(features, coordinate);
	}
    }
};


// from: http://openlayers.org/en/v3.9.0/examples/vector-labels.html
var getText = function(feature, resolution) {
    var type = 'normal';
    var maxResolution = 2;
    var text = feature.get('Name');

    if ( true ) {
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
    var size = '12pt';
    var offsetX = 5;
    var offsetY = -5;
    var weight = 'bold';
    var rotation = 0;
    var font = weight + ' ' + size + ' Arial';
    var fillColor = '#ffffff';
    var outlineColor = '#000000';
    var outlineWidth = 3;

    return new ol.style.Text({
	textAlign: align,
	textBaseline: baseline,
	font: font,
	color: "#ffffff",
	text: getText(feature, resolution),
	fill: new ol.style.Fill({color: fillColor}),
	stroke: new ol.style.Stroke({color: outlineColor, width: outlineWidth}),
	offsetX: offsetX,
	offsetY: offsetY,
	rotation: rotation
    });
};

function setLayer(fn) {
    if ( fn.length <= 1 ) {
	alert("That image is not available -- see README.md and re-run mcpe_viz");
	return -1;
    }
    
    // note: an attempt to add the new layer and then remove the old layer to prevent white screen (doesn't work)
    // todo - attribution is small and weird in map - why?
    var src = new ol.source.ImageStatic({
	attributions: [
	    new ol.Attribution({
		html: 'Created by <a href="https://github.com/Plethora777/mcpe_viz" target="_blank">mcpe_viz</a>'
	    })
	],
	url: fn,
	projection: projection,
	imageSize: [ dimensionInfo[globalDimensionId].worldWidth, dimensionInfo[globalDimensionId].worldHeight ],
	// "Extent of the image in map coordinates. This is the [left, bottom, right, top] map coordinates of your image."
	imageExtent: extent
    });

    src.on('imageloadstart', function(event) {
	updateLoadEventCount(1);
    });
    src.on('imageloadend', function(event) {
	updateLoadEventCount(-1);
    });
    src.on('imageloaderror', function(event) {
	updateLoadEventCount(-1);
	alert("Image load error.  Filename=" + fn);
    });
    
    if ( layerMain === null ) {
	layerMain = new ol.layer.Image({source: src});
	map.addLayer(layerMain);

	// get the pixel position with every move
	$(map.getViewport()).on('mousemove', function(evt) {
	    globalMousePosition = map.getEventPixel(evt.originalEvent);
	    // todo - too expensive?
	    map.render();
	}).on('mouseout', function() {
	    globalMousePosition = null;
	    // todo - too expensive?
	    map.render();
	});
	
	layerMain.on('postcompose', function(event) {
	    var ctx = event.context;
	    var pixelRatio = event.frameState.pixelRatio;
	    pixelDataName="";
	    if ( globalMousePosition && globalLayerMode===0 && globalLayerId===0 ) {
		var x = globalMousePosition[0] * pixelRatio;
		var y = globalMousePosition[1] * pixelRatio;
		pixelData = ctx.getImageData(x, y, 1, 1).data;
		var cval = (pixelData[0] << 16) | (pixelData[1] << 8) | pixelData[2];
		pixelDataName = blockColorLUT[""+cval];
		if ( pixelDataName === undefined ) {
		    pixelDataName = "Unknown RGB: " + pixelData[0] +" "+ pixelData[1] +" "+ pixelData[2] + " ("+cval+")";
		}
	    }
	});
	
	var bindLayerListeners = function(layer) {
	    layer.on('precompose', setCanvasSmoothingMode);
	    layer.on('postcompose', resetCanvasSmoothingMode);
	};
	bindLayerListeners(layerMain);
    } else {
	layerMain.setSource( src );
    }
    return 0;
}


function setLayerById(id) {
    if ( 0 ) {
    } else if ( id === 1 ) {
	if ( setLayer(dimensionInfo[globalDimensionId].fnLayerBiome) === 0 ) {
	    globalLayerMode = 0; globalLayerId = 1;
	    $("#imageSelectName").html("Biome");
	}
    } else if ( id === 2 ) {
	if ( setLayer(dimensionInfo[globalDimensionId].fnLayerHeight) === 0 ) {
	    globalLayerMode = 0; globalLayerId = 2;
	    $("#imageSelectName").html("Height");
	}
    } else if ( id === 3 ) {
	if ( setLayer(dimensionInfo[globalDimensionId].fnLayerBlockLight) === 0 ) {
	    globalLayerMode = 0; globalLayerId = 3;
	    $("#imageSelectName").html("Block Light");
	}
    } else if ( id === 4 ) {
	if ( setLayer(dimensionInfo[globalDimensionId].fnLayerGrass) === 0 ) {
	    globalLayerMode = 0; globalLayerId = 4;
	    $("#imageSelectName").html("Grass Color");
	}
    } else {
	// default is overview map
	if ( setLayer(dimensionInfo[globalDimensionId].fnLayerTop) === 0 ) {
	    globalLayerMode = 0; globalLayerId = 0;
	    $("#imageSelectName").html("Overview");
	}
    }
}


function initDimension() {
    // Map views always need a projection.  Here we just want to map image
    // coordinates directly to map coordinates, so we create a projection that uses
    // the image extent in pixels.
    dimensionInfo[globalDimensionId].worldWidth  =
	dimensionInfo[globalDimensionId].maxWorldX - dimensionInfo[globalDimensionId].minWorldX + 1;
    dimensionInfo[globalDimensionId].worldHeight =
	dimensionInfo[globalDimensionId].maxWorldY - dimensionInfo[globalDimensionId].minWorldY + 1;

    extent = [ 0,0,
	       dimensionInfo[globalDimensionId].worldWidth - 1,
	       dimensionInfo[globalDimensionId].worldHeight - 1 ];

    dimensionInfo[globalDimensionId].globalOffsetX = dimensionInfo[globalDimensionId].minWorldX;
    dimensionInfo[globalDimensionId].globalOffsetY = dimensionInfo[globalDimensionId].minWorldY;

    console.log("World bounds: dimId="+globalDimensionId
		+ " w=" + dimensionInfo[globalDimensionId].worldWidth
		+ " h=" + dimensionInfo[globalDimensionId].worldHeight
		+ " offx=" + dimensionInfo[globalDimensionId].globalOffsetX
		+ " offy=" + dimensionInfo[globalDimensionId].globalOffsetY
	       );
    
    projection = new ol.proj.Projection({
	code: 'mcpe_viz-image',
	units: 'm',
	extent: extent,
	getPointResolution: function(resolution, coordinate) {
	    return resolution;
	}
    });

    if ( mousePositionControl === null ) {
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
    
    var attribution = new ol.control.Attribution({
	collapsed: false,
	collapsible: false
	//target: "_blank"
    });		

    if ( map === null ) {
	map = new ol.Map({
	    controls: ol.control.defaults({ attribution:false })
		.extend([
		    attribution,
		    new ol.control.ZoomToExtent(),
		    new ol.control.ScaleLine(),
		    new ol.control.FullScreen(),
		    mousePositionControl
		]),
	    pixelRatio: 1, // todo - need this?
	    target: 'map',
	    view: new ol.View({
		projection: projection,
		center: [ dimensionInfo[globalDimensionId].playerPosX, dimensionInfo[globalDimensionId].playerPosY],
		zoom: 6
	    })
	});
    } else {
	var view = new ol.View({
	    projection: projection,
	    center: [ dimensionInfo[globalDimensionId].playerPosX, dimensionInfo[globalDimensionId].playerPosY],
	    zoom: 6
	});
	map.setView(view);
    }

    // finally load the proper layer
    if ( globalLayerMode === 0 ) {
	return setLayerById( globalLayerId );
    } else {
	return layerGoto(layerRawIndex);
    }
}

function setDimensionById(id) {
    var prevDID = globalDimensionId;
    if ( 0 ) {
    }
    else if ( id === 1 ) {
	globalDimensionId = id;
	$("#dimensionSelectName").html("Nether");
    }
    else {
	// default to overworld
	globalDimensionId = id;
	$("#dimensionSelectName").html("Overworld");
    }

    if ( prevDID !== globalDimensionId ) {
	initDimension();
    }
}


var createPointStyleFunction = function() {
    return function(feature, resolution) {
	var style;
	var entity = feature.get("Entity");
	var tileEntity = feature.get("TileEntity");
	var did = feature.get("Dimension");

	// hack for pre-0.12 worlds
	if ( did === undefined ) {
	    did = 0;
	} else {
	    did = +did;
	}
	
	if ( entity !== undefined ) {
	    if ( did === globalDimensionId ) {
		var id = +feature.get("id");
		if ( listEntityToggle[id] !== undefined ) {
		    if ( listEntityToggle[id] ) { 
			style = new ol.style.Style({
			    image: new ol.style.Circle({
				radius: 5,
				fill: new ol.style.Fill({color: 'rgba(255, 255, 255, 0.5)'}),
				stroke: new ol.style.Stroke({color: 'rgba(0, 0, 0, 0.5)', width: 2})
			    }),
			    text: createTextStyle(feature, resolution)
			});
			return [style];
		    }
		}
	    }
	}
	else if ( tileEntity !== undefined ) {
	    if ( did === globalDimensionId ) {
		var Name = feature.get("Name");
		if ( listTileEntityToggle[Name] !== undefined ) {
		    if ( listTileEntityToggle[Name] ) { 
			style = new ol.style.Style({
			    image: new ol.style.Circle({
				radius: 5,
				fill: new ol.style.Fill({color: 'rgba(255, 255, 255, 0.5)'}),
				stroke: new ol.style.Stroke({color: 'rgba(0, 0, 0, 0.5)', width: 2})
			    }),
			    text: createTextStyle(feature, resolution)
			});
			return [style];
		    }
		}
	    }
	}
	else {
	    console.log("Weird.  Found a GeoJSON item that is not an entity or a tileEntitiy");
	}
	return null;
    };
};

var vectorPoints = null;

function loadVectors() {
    if ( vectorPoints !== null ) {
	map.removeLayer(vectorPoints);
    }

    var src = new ol.source.Vector({
	url: dimensionInfo[globalDimensionId].fnGeoJSON,
	format: new ol.format.GeoJSON()
    });
    // todo - are there events like this for vectors?
    src.on('imageloadstart', function(event) {
	updateLoadEventCount(1);
    });
    src.on('imageloadend', function(event) {
	updateLoadEventCount(-1);
    });
    src.on('imageloaderror', function(event) {
	updateLoadEventCount(-1);
    });

    vectorPoints = new ol.layer.Vector({
	source: src,
	style: createPointStyleFunction()
    });
    
    map.addLayer(vectorPoints);
}


function entityToggle(id) {
    id = +id;
    if ( vectorPoints === null ) {
	loadVectors();
    }
    if ( listEntityToggle[id] === undefined ) {
	listEntityToggle[id] = true;
    } else {
	listEntityToggle[id] = !listEntityToggle[id];
    }
    vectorPoints.changed();
}

function tileEntityToggle(name) {
    if ( vectorPoints === null ) {
	loadVectors();
    }
    if ( listTileEntityToggle[name] === undefined ) {
	listTileEntityToggle[name] = true;
    } else {
	listTileEntityToggle[name] = !listTileEntityToggle[name];
    }
    vectorPoints.changed();
}

function layerMove(delta) {
    //this_.getMap().getView().setRotation(0);
    layerRawIndex += delta;
    if ( layerRawIndex < 0 ) { layerRawIndex=0; }
    if ( layerRawIndex > 127 ) { layerRawIndex=127; }
    layerGoto(layerRawIndex);
}

function layerGoto(layer) {
    if ( layer < 0 ) { layer=0; }
    if ( layer > 127 ) { layer=127; }
    if ( setLayer(dimensionInfo[globalDimensionId].listLayers[layer]) === 0 ) {
	globalLayerMode = 1;
	layerRawIndex = layer;
	$("#layerNumber").html(""+layer);
    }
}


// todo - this is still not quiet right
var coordinateFormatFunction = function(coordinate) {
    cx = coordinate[0] + dimensionInfo[globalDimensionId].globalOffsetX;
    cy = ((dimensionInfo[globalDimensionId].worldHeight - 1) - coordinate[1]) + dimensionInfo[globalDimensionId].globalOffsetY;
    ix = coordinate[0];
    iy = (dimensionInfo[globalDimensionId].worldHeight - 1) - coordinate[1];
    var prec=1;
    var s = "world: " + cx.toFixed(prec) + " " + cy.toFixed(prec) + " image: " + ix.toFixed(prec) + " " + iy.toFixed(prec);
    if ( pixelDataName.length > 0 ) {
	s += "<br/>Block: " + pixelDataName;
    }
    return s;
};

// adapted from: http://stackoverflow.com/questions/12887506/cannot-set-maps-div-height-for-openlayers-map
var fixContentHeight = function(){
    var viewHeight = $(window).height();
    var navbar = $("div[data-role='navbar']:visible:visible");
    var newMapH = viewHeight - navbar.outerHeight();
    var curMapSize = map.getSize();
    curMapSize[1] = newMapH;
    map.setSize(curMapSize);
};

$(function() {

    // setup tooltips
    $('.mytooltip').tooltip({
	// this helps w/ btn groups
	trigger: "hover",
	container: "body"
    });
    
    // add the main layer
    setDimensionById(0);

    popover = new ol.Overlay({
	element: document.getElementById('popover'),
	autoPan: true,
	autoPanAnimation: {
	    duration: 100
	}
    });
    map.addOverlay(popover);


    // todo - refine overview map cfg?
    if ( false ) {
	var omap = new ol.control.OverviewMap({
	    layers: [layerMain]
	});
	map.addControl(omap);
    }
    
    map.on('singleclick', function(evt) {
	displayFeatureInfo(evt);
    });

    $(".dimensionSelect").click(function() {
	var id = +$(this).attr("data-id");
	setDimensionById(id);
    });
    
    
    $("#layerPrev").click(function() { layerMove(-1); });
    $("#layerNext").click(function() { layerMove(1); });
    
    $(".imageSelect").click(function() {
	var id = +$(this).attr("data-id");
	setLayerById(id);
    });
    
    $(".entityToggleRemoveAll").click(function() {
	listEntityToggle = [];
	if ( vectorPoints !== null ) { 
	    vectorPoints.changed();
	}
	$(".entityToggle").parent().removeClass("active");
    });
    $(".entityToggle").click(function() {
	var id = $(this).attr("data-id");
	entityToggle(id);
	if ( listEntityToggle[id] ) {
	    $(".entityToggle[data-id=" + id + "]").parent().addClass("active");
	} else {
	    $(".entityToggle[data-id=" + id + "]").parent().removeClass("active");
	}
    });

    $(".tileEntityToggleRemoveAll").click(function() {
	listTileEntityToggle = [];
	if ( vectorPoints !== null ) { 
	    vectorPoints.changed();
	}
	$(".tileEntityToggle").parent().removeClass("active");
    });
    $(".tileEntityToggle").click(function() {
	var id = $(this).attr("data-id");
	tileEntityToggle(id);
	if ( listTileEntityToggle[id] ) {
	    $(".tileEntityToggle[data-id=" + id + "]").parent().addClass("active");
	} else {
	    $(".tileEntityToggle[data-id=" + id + "]").parent().removeClass("active");
	}
    });

    // fix map size
    window.addEventListener('resize', fixContentHeight);
    fixContentHeight();
});
