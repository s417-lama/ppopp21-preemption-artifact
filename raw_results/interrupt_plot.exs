filename = System.argv() |> Enum.at(0)

files = [
    "timer_cost_naive/*.out",
    "timer_cost_managed/*.out",
    "timer_cost_central/*.out",
    "timer_cost_chain/*.out",
]

opts = [
    {"Per-worker (creation-time)", "#1f77b4", "dashdot", "square-open" },
    {"Per-worker (aligned)"      , "#1f77b4", "solid"  , "diamond-open"},
    {"Per-process (one-to-all)"  , "#ff7f0e", "dashdot", "cross-thin"  },
    {"Per-process (chain)"       , "#ff7f0e", "solid"  , "x-thin"      },
]

linewidth = 2

Enum.zip(files, opts)
|> Enum.map(fn {file, opt} ->
    {title, color, dash, symbol} = opt
    {xs, ts} =
        file
        |> Path.expand()
        |> Path.wildcard()
        |> Enum.map(fn path ->
            in_data =
                File.stream!(path)
                |> Stream.filter(fn line -> String.contains?(line, "signal handler") end)
                |> Stream.map(&String.split/1)
                |> Stream.map(fn list -> (Enum.at(list, 7) |> String.to_integer()) / 1000 end)
                |> Enum.to_list()
            out_data =
                File.stream!(path)
                |> Stream.filter(fn line -> String.contains?(line, "normal context") end)
                |> Stream.map(&String.split/1)
                |> Stream.map(fn list -> (Enum.at(list, 8) |> String.to_integer()) / 1000 end)
                |> Enum.to_list()
            {length(in_data) - 1, List.last(in_data) + List.last(out_data)}
        end)
        |> Enum.group_by(fn {r, _t} -> r end, fn {_r, t} -> t end)
        |> Enum.sort()
        |> Enum.unzip()
    {ys, errors} =
        Enum.map(ts, fn t ->
          {Statistics.mean(t), Statistics.stdev(t)}
        end)
        |> Enum.unzip()
    %{
        x: xs,
        y: ys,
        error_y: %{
            type:      "data",
            array:     errors,
            visible:   true,
            thickness: linewidth,
        },
        name: title,
        line: %{
            color: color,
            width: linewidth,
            dash:  dash,
        },
        marker: %{
            symbol: symbol,
            size:   14,
            line:   %{width: linewidth, color: color},
        },
        type: "scatter",
    }
end)
|> PlotlyEx.plot(%{
    width:  750,
    height: 350,
    xaxis: %{
        type: "log",
        ticks: "outside",
        range: [-0.1,2.05],
        tickangle: 0,
        showline: true,
        tickmode: 'array',
        tickvals: [1,2,3,4,5,6,7,8,9,10,20,30,40,50,60,70,80,90,100],
        ticktext: [1,"","","","","","","","",10,"","","","","","","","",100],
        title: %{text: "# of workers"},
    },
    yaxis: %{
        type:  "log",
        ticks: "outside",
        # range: [0,2.3],
        range: [0,2.01],
        showline: true,
        tickmode: 'array',
        tickvals: [1,2,3,4,5,6,7,8,9,10,20,30,40,50,60,70,80,90,100,200,300,400,500,600,700,800,900,1000],
        ticktext: [1,"","","","","","","","",10,"","","","","","","","",100,"","","","","","","","",1000],
        title: %{text: "Interruption Time (&#x03BC;s)"},
    },
    showlegend: true,
    # legend: %{
    #     x: 0,
    #     y: 1,
    # },
    font: %{
        size: 22,
    },
    margin: %{
        l: 80,
        r: 10,
        b: 70,
        t: 10,
        pad: 0,
    },
})
|> PlotlyEx.show(filename: filename)
