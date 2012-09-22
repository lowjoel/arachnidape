var window = new (function() {
	var consoleType = function() {
		//Output functions.
		function printOutput(printer, level, x) {
			//Generates a time string.
			function timeString() {
				function padZeroes(number, width) {
					var result = new String(number);
					while (result.length < width) {
						result = "0" + result;
					}
					
					return result;
				}
				var date = new Date();
				return padZeroes(date.getHours(), 2) + ":" +
					padZeroes(date.getMinutes(), 2) + ":" +
					padZeroes(date.getSeconds(), 2) + "." +
					padZeroes(date.getMilliseconds(), 3);
			}
			
			//Formats an object for display.
			function format(x) {
				if ((Array.isArray && Array.isArray(x)) ||
					Object.prototype.toString.call(x) === '[object Array]') {
					var result = "[";
					
					for (var i = 0, j = x.length; i < j; ++i) {
						result += format(x[i]) + ", ";
					}
					
					if (x.length) {
						result = result.substr(0, result.length - 2);
					}
					
					result += "]";
					return result;
				} else if (typeof x === "string") {
					return '"' + x + '"';
				}
				
				return x;
			}
			
			//Print the output.
			return printer("    <" + level + "> " + timeString() + ": " + format(x));
		}
		
		consoleType.prototype.log = function(x) {
			printOutput(print, 1, x);
		}
		consoleType.prototype.info = function(x) {
			printOutput(print, 2, x);
		}
		consoleType.prototype.warn = function(x) {
			printOutput(printErr, 3, x);
		}
		consoleType.prototype.error = function(x) {
			printOutput(printErr, 4, x);
		}
		//Deprecated
		consoleType.prototype.debug = consoleType.prototype.log;
	};
	
	this.console = new consoleType();
	this.alert = print;
	this.prompt = function(message) {
		print(message + ": ");
		return readline();
	}
});

with (window) {
	//Insert code here.
}
