export namespace main {
	
	export class TaskSummary {
	    taskName: string;
	    taskColor: string;
	    totalSeconds: number;
	
	    static createFrom(source: any = {}) {
	        return new TaskSummary(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.taskName = source["taskName"];
	        this.taskColor = source["taskColor"];
	        this.totalSeconds = source["totalSeconds"];
	    }
	}
	export class DaySummary {
	    date: string;
	    totalSeconds: number;
	    taskCount: number;
	
	    static createFrom(source: any = {}) {
	        return new DaySummary(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.date = source["date"];
	        this.totalSeconds = source["totalSeconds"];
	        this.taskCount = source["taskCount"];
	    }
	}
	export class AnalyticsData {
	    dailySummary: DaySummary[];
	    taskSummary: TaskSummary[];
	    totalSeconds: number;
	
	    static createFrom(source: any = {}) {
	        return new AnalyticsData(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.dailySummary = this.convertValues(source["dailySummary"], DaySummary);
	        this.taskSummary = this.convertValues(source["taskSummary"], TaskSummary);
	        this.totalSeconds = source["totalSeconds"];
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class TimeEntry {
	    id: number;
	    taskId: number;
	    taskName: string;
	    taskColor: string;
	    startTime: string;
	    endTime: string;
	    duration: number;
	    note: string;
	
	    static createFrom(source: any = {}) {
	        return new TimeEntry(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.taskId = source["taskId"];
	        this.taskName = source["taskName"];
	        this.taskColor = source["taskColor"];
	        this.startTime = source["startTime"];
	        this.endTime = source["endTime"];
	        this.duration = source["duration"];
	        this.note = source["note"];
	    }
	}
	export class DashboardData {
	    todaySeconds: number;
	    weekSeconds: number;
	    totalSeconds: number;
	    taskCount: number;
	    recentEntries: TimeEntry[];
	    weekDaySummary: DaySummary[];
	
	    static createFrom(source: any = {}) {
	        return new DashboardData(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.todaySeconds = source["todaySeconds"];
	        this.weekSeconds = source["weekSeconds"];
	        this.totalSeconds = source["totalSeconds"];
	        this.taskCount = source["taskCount"];
	        this.recentEntries = this.convertValues(source["recentEntries"], TimeEntry);
	        this.weekDaySummary = this.convertValues(source["weekDaySummary"], DaySummary);
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	
	export class PomodoroSettings {
	    focusDuration: number;
	    shortBreakDuration: number;
	    longBreakDuration: number;
	    longBreakInterval: number;
	
	    static createFrom(source: any = {}) {
	        return new PomodoroSettings(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.focusDuration = source["focusDuration"];
	        this.shortBreakDuration = source["shortBreakDuration"];
	        this.longBreakDuration = source["longBreakDuration"];
	        this.longBreakInterval = source["longBreakInterval"];
	    }
	}
	export class Task {
	    id: number;
	    name: string;
	    color: string;
	    createdAt: string;
	
	    static createFrom(source: any = {}) {
	        return new Task(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.name = source["name"];
	        this.color = source["color"];
	        this.createdAt = source["createdAt"];
	    }
	}
	

}

